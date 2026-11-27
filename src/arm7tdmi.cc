#include "arm7tdmi.h"
#include "gba_memory.h"

namespace
{
    const TUint32 KFlagN = 0x80000000u;
    const TUint32 KFlagZ = 0x40000000u;
    const TUint32 KFlagC = 0x20000000u;
    const TUint32 KFlagV = 0x10000000u;
    const TUint32 KFlagI = 0x00000080u;
    const TUint32 KFlagT = 0x00000020u;

    const TUint32 KModeMask = 0x0000001Fu;
    const TUint32 KModeUser = 0x00000010u;
    const TUint32 KModeFiq  = 0x00000011u;
    const TUint32 KModeIrq  = 0x00000012u;
    const TUint32 KModeSvc  = 0x00000013u;
    const TUint32 KModeAbt  = 0x00000017u;
    const TUint32 KModeUnd  = 0x0000001Bu;
    const TUint32 KModeSys  = 0x0000001Fu;

    const TUint32 KRomBase = 0x08000000u;
    const TUint32 KHleIrqReturnAddress = 0x00000138u;

    struct TCpuBankState
    {
        TUint32 r8_usr[5];
        TUint32 r8_fiq[5];
        TUint32 r13_usr;
        TUint32 r14_usr;
        TUint32 r13_fiq;
        TUint32 r14_fiq;
        TUint32 r13_irq;
        TUint32 r14_irq;
        TUint32 r13_svc;
        TUint32 r14_svc;
        TUint32 r13_abt;
        TUint32 r14_abt;
        TUint32 r13_und;
        TUint32 r14_und;
        TUint32 spsr_fiq;
        TUint32 spsr_irq;
        TUint32 spsr_svc;
        TUint32 spsr_abt;
        TUint32 spsr_und;
        TUint16 intrWaitMask;
        TBool irqTrampolineActive;
    };

    TCpuBankState gBank;

    // Tabel sinus kuadran pertama (0..64 -> 0..90 derajat), format 8.8 (256 = 1.0).
    const TInt16 KSinQuarterTable[65] =
    {
          0,   6,  13,  19,  25,  31,  38,  44,
         50,  56,  62,  68,  74,  80,  86,  92,
         98, 104, 109, 115, 121, 126, 132, 137,
        142, 147, 152, 157, 162, 167, 172, 177,
        181, 185, 190, 194, 198, 202, 206, 209,
        213, 216, 220, 223, 226, 229, 231, 234,
        237, 239, 241, 243, 245, 247, 248, 250,
        251, 252, 253, 254, 255, 255, 256, 256,
        256
    };

    // Tabel 2^(-n/12) dalam format 16.16 untuk n = 0..12 (MidiKey2Freq).
    const TUint32 KSemitoneInvTable[13] =
    {
        65536u, 61858u, 58386u, 55109u,
        52016u, 49097u, 46341u, 43740u,
        41285u, 38968u, 36781u, 34716u,
        32768u
    };

    TInt32 FixedSin8_8(TUint16 angle)
    {
        const TUint32 index = (static_cast<TUint32>(angle) >> 8) & 0xFFu;
        const TUint32 frac = static_cast<TUint32>(angle) & 0xFFu;
        const TUint32 quadrant = (index >> 6) & 3u;
        const TUint32 pos = index & 0x3Fu;

        TInt32 s0 = 0;
        TInt32 s1 = 0;

        if (quadrant == 0)
        {
            s0 = KSinQuarterTable[pos];
            s1 = KSinQuarterTable[pos + 1];
        }
        else if (quadrant == 1)
        {
            s0 = KSinQuarterTable[64 - pos];
            s1 = KSinQuarterTable[63 - pos];
        }
        else if (quadrant == 2)
        {
            s0 = -KSinQuarterTable[pos];
            s1 = -KSinQuarterTable[pos + 1];
        }
        else
        {
            s0 = -KSinQuarterTable[64 - pos];
            s1 = -KSinQuarterTable[63 - pos];
        }

        return s0 + (((s1 - s0) * static_cast<TInt32>(frac)) >> 8);
    }

    TInt32 FixedCos8_8(TUint16 angle)
    {
        return FixedSin8_8(static_cast<TUint16>(angle + 0x4000u));
    }

    void SetThumbState(TNgbaCpuState& cpu, TBool thumb)
    {
        cpu.thumb = thumb;

        if (thumb)
            cpu.cpsr |= KFlagT;
        else
            cpu.cpsr &= ~KFlagT;
    }

    void SwitchCpuMode(TNgbaCpuState& cpu, TUint32 newMode)
    {
        const TUint32 oldMode = cpu.cpsr & KModeMask;
        newMode &= KModeMask;

        if (oldMode == newMode)
            return;

        if (oldMode == KModeFiq)
        {
            for (TInt i = 0; i < 5; ++i)
            {
                gBank.r8_fiq[i] = cpu.r[8 + i];
                cpu.r[8 + i] = gBank.r8_usr[i];
            }
        }
        else if (newMode == KModeFiq)
        {
            for (TInt i = 0; i < 5; ++i)
            {
                gBank.r8_usr[i] = cpu.r[8 + i];
                cpu.r[8 + i] = gBank.r8_fiq[i];
            }
        }

        switch (oldMode)
        {
        case KModeFiq:
            gBank.r13_fiq = cpu.r[13];
            gBank.r14_fiq = cpu.r[14];
            break;
        case KModeIrq:
            gBank.r13_irq = cpu.r[13];
            gBank.r14_irq = cpu.r[14];
            break;
        case KModeSvc:
            gBank.r13_svc = cpu.r[13];
            gBank.r14_svc = cpu.r[14];
            break;
        case KModeAbt:
            gBank.r13_abt = cpu.r[13];
            gBank.r14_abt = cpu.r[14];
            break;
        case KModeUnd:
            gBank.r13_und = cpu.r[13];
            gBank.r14_und = cpu.r[14];
            break;
        default:
            gBank.r13_usr = cpu.r[13];
            gBank.r14_usr = cpu.r[14];
            break;
        }

        switch (newMode)
        {
        case KModeFiq:
            cpu.r[13] = gBank.r13_fiq;
            cpu.r[14] = gBank.r14_fiq;
            break;
        case KModeIrq:
            cpu.r[13] = gBank.r13_irq;
            cpu.r[14] = gBank.r14_irq;
            break;
        case KModeSvc:
            cpu.r[13] = gBank.r13_svc;
            cpu.r[14] = gBank.r14_svc;
            break;
        case KModeAbt:
            cpu.r[13] = gBank.r13_abt;
            cpu.r[14] = gBank.r14_abt;
            break;
        case KModeUnd:
            cpu.r[13] = gBank.r13_und;
            cpu.r[14] = gBank.r14_und;
            break;
        default:
            cpu.r[13] = gBank.r13_usr;
            cpu.r[14] = gBank.r14_usr;
            break;
        }

        cpu.cpsr = (cpu.cpsr & ~KModeMask) | newMode;
    }

    TUint32 GetCurrentSpsr(const TNgbaCpuState& cpu)
    {
        switch (cpu.cpsr & KModeMask)
        {
        case KModeFiq: return gBank.spsr_fiq;
        case KModeIrq: return gBank.spsr_irq;
        case KModeSvc: return gBank.spsr_svc;
        case KModeAbt: return gBank.spsr_abt;
        case KModeUnd: return gBank.spsr_und;
        default:       return cpu.cpsr;
        }
    }

    void SetCurrentSpsr(TNgbaCpuState& cpu, TUint32 value)
    {
        switch (cpu.cpsr & KModeMask)
        {
        case KModeFiq: gBank.spsr_fiq = value; break;
        case KModeIrq: gBank.spsr_irq = value; break;
        case KModeSvc: gBank.spsr_svc = value; break;
        case KModeAbt: gBank.spsr_abt = value; break;
        case KModeUnd: gBank.spsr_und = value; break;
        default:       break;
        }
    }

    void SetCpsrWithMode(TNgbaCpuState& cpu, TUint32 newCpsr)
    {
        SwitchCpuMode(cpu, newCpsr & KModeMask);
        cpu.cpsr = newCpsr;
        SetThumbState(cpu, (newCpsr & KFlagT) != 0);
    }

    TUint32 ReadUserRegister(const TNgbaCpuState& cpu, TInt index)
    {
        const TUint32 mode = cpu.cpsr & KModeMask;

        if (index >= 8 && index <= 12 && mode == KModeFiq)
            return gBank.r8_usr[index - 8];

        if (index == 13 && mode != KModeUser && mode != KModeSys)
            return gBank.r13_usr;

        if (index == 14 && mode != KModeUser && mode != KModeSys)
            return gBank.r14_usr;

        return cpu.r[index & 15];
    }

    void WriteUserRegister(TNgbaCpuState& cpu, TInt index, TUint32 value)
    {
        const TUint32 mode = cpu.cpsr & KModeMask;

        if (index >= 8 && index <= 12 && mode == KModeFiq)
        {
            gBank.r8_usr[index - 8] = value;
            return;
        }

        if (index == 13 && mode != KModeUser && mode != KModeSys)
        {
            gBank.r13_usr = value;
            return;
        }

        if (index == 14 && mode != KModeUser && mode != KModeSys)
        {
            gBank.r14_usr = value;
            return;
        }

        cpu.r[index & 15] = value;
    }

    TBool ConditionPassed(TUint32 cpsr, TUint32 condition)
    {
        const TBool n = (cpsr & KFlagN) != 0;
        const TBool z = (cpsr & KFlagZ) != 0;
        const TBool c = (cpsr & KFlagC) != 0;
        const TBool v = (cpsr & KFlagV) != 0;

        switch (condition)
        {
        case 0x0: return z;
        case 0x1: return !z;
        case 0x2: return c;
        case 0x3: return !c;
        case 0x4: return n;
        case 0x5: return !n;
        case 0x6: return v;
        case 0x7: return !v;
        case 0x8: return c && !z;
        case 0x9: return !c || z;
        case 0xA: return n == v;
        case 0xB: return n != v;
        case 0xC: return !z && (n == v);
        case 0xD: return z || (n != v);
        case 0xE: return ETrue;
        default: return EFalse;
        }
    }

    TUint32 ReadRegister(
        const TNgbaCpuState& cpu,
        TInt index,
        TUint32 address
    )
    {
        if (index == 15)
        {
            if (cpu.thumb)
                return address + 4u;

            return address + 8u;
        }

        return cpu.r[index & 15];
    }

    void SetNzFlags(TNgbaCpuState& cpu, TUint32 value)
    {
        cpu.cpsr &= ~(KFlagN | KFlagZ);

        if (value & 0x80000000u)
            cpu.cpsr |= KFlagN;

        if (value == 0)
            cpu.cpsr |= KFlagZ;
    }

    void SetCarry(TNgbaCpuState& cpu, TBool carry)
    {
        cpu.cpsr &= ~KFlagC;

        if (carry)
            cpu.cpsr |= KFlagC;
    }

    TUint32 RotateRight(TUint32 value, TInt amount)
    {
        amount &= 31;

        if (amount == 0)
            return value;

        return (value >> amount) |
               (value << (32 - amount));
    }

    TUint32 ReadWordRotated(TUint32 address)
    {
        const TUint32 alignedAddress = address & ~3u;
        const TUint32 value = NgbaMemoryRead32(alignedAddress);
        const TInt rotation =
            static_cast<TInt>(address & 3u) * 8;

        return RotateRight(value, rotation);
    }

    TUint32 ReadHalfwordGba(TUint32 address)
    {
        const TUint32 value = NgbaMemoryRead16(address & ~1u);

        if (address & 1u)
            return RotateRight(value, 8);

        return value;
    }

    TUint32 ShiftValue(
        TUint32 value,
        TInt type,
        TInt amount,
        TBool& carry
    )
    {
        if (amount == 0)
            return value;

        if (type == 0)
        {
            if (amount >= 32)
            {
                carry = (amount == 32)
                    ? ((value & 1u) != 0)
                    : EFalse;

                return 0;
            }

            carry = ((value >> (32 - amount)) & 1u) != 0;
            return value << amount;
        }

        if (type == 1)
        {
            if (amount >= 32)
            {
                carry = (amount == 32)
                    ? ((value & 0x80000000u) != 0)
                    : EFalse;

                return 0;
            }

            carry = ((value >> (amount - 1)) & 1u) != 0;
            return value >> amount;
        }

        if (type == 2)
        {
            if (amount >= 32)
            {
                carry = (value & 0x80000000u) != 0;
                return carry ? 0xFFFFFFFFu : 0;
            }

            carry = ((value >> (amount - 1)) & 1u) != 0;

            if (value & 0x80000000u)
                return (value >> amount) |
                       (~0u << (32 - amount));

            return value >> amount;
        }

        amount &= 31;

        if (amount == 0)
        {
            carry = (value & 0x80000000u) != 0;
            return value;
        }

        carry = ((value >> (amount - 1)) & 1u) != 0;
        return RotateRight(value, amount);
    }

    void SetArithmeticFlags(
        TNgbaCpuState& cpu,
        TUint32 left,
        TUint32 right,
        TUint32 result,
        TBool subtract,
        TUint32 carryIn = 0
    )
    {
        SetNzFlags(cpu, result);

        cpu.cpsr &= ~(KFlagC | KFlagV);

        if (subtract)
        {
            const TUint64 subtrahend =
                static_cast<TUint64>(right) +
                static_cast<TUint64>(carryIn);

            if (static_cast<TUint64>(left) >= subtrahend)
                cpu.cpsr |= KFlagC;

            const TInt64 signedLeft =
                static_cast<TInt64>(
                    static_cast<TInt32>(left)
                );

            const TInt64 signedRight =
                static_cast<TInt64>(
                    static_cast<TInt32>(right)
                );

            const TInt64 signedResult =
                signedLeft - signedRight -
                static_cast<TInt64>(carryIn);

            if (signedResult > 2147483647LL ||
                signedResult < (-2147483647LL - 1LL))
            {
                cpu.cpsr |= KFlagV;
            }
        }
        else
        {
            const TUint64 wide =
                static_cast<TUint64>(left) +
                static_cast<TUint64>(right) +
                static_cast<TUint64>(carryIn);

            if (wide >> 32)
                cpu.cpsr |= KFlagC;

            const TInt64 signedLeft =
                static_cast<TInt64>(
                    static_cast<TInt32>(left)
                );

            const TInt64 signedRight =
                static_cast<TInt64>(
                    static_cast<TInt32>(right)
                );

            const TInt64 signedResult =
                signedLeft + signedRight +
                static_cast<TInt64>(carryIn);

            if (signedResult > 2147483647LL ||
                signedResult < (-2147483647LL - 1LL))
            {
                cpu.cpsr |= KFlagV;
            }
        }
    }

    TUint32 DecodeOperand2(
        const TNgbaCpuState& cpu,
        TUint32 instruction,
        TUint32 address,
        TBool& carry
    )
    {
        carry = (cpu.cpsr & KFlagC) != 0;

        if (instruction & (1u << 25))
        {
            const TInt rotate =
                static_cast<TInt>((instruction >> 8) & 15) * 2;

            const TUint32 value =
                RotateRight(instruction & 0xFFu, rotate);

            if (rotate != 0)
                carry = (value & 0x80000000u) != 0;

            return value;
        }

        const TInt rm = instruction & 15;
        TUint32 value = ReadRegister(cpu, rm, address);
        const TInt type = (instruction >> 5) & 3;

        if (instruction & (1u << 4))
        {
            const TInt rs = (instruction >> 8) & 15;
            const TInt amount = static_cast<TInt>(
                ReadRegister(cpu, rs, address) & 0xFFu
            );

            if (amount == 0)
                return value;

            return ShiftValue(value, type, amount, carry);
        }

        const TInt amount = (instruction >> 7) & 31;

        if (amount == 0)
        {
            if (type == 0)
                return value;

            if (type == 1)
            {
                carry = (value & 0x80000000u) != 0;
                return 0;
            }

            if (type == 2)
            {
                carry = (value & 0x80000000u) != 0;
                return carry ? 0xFFFFFFFFu : 0;
            }

            carry = (value & 1u) != 0;

            return (value >> 1) |
                   ((cpu.cpsr & KFlagC) ? 0x80000000u : 0);
        }

        return ShiftValue(value, type, amount, carry);
    }

    // ---------------------------------------------------------
    // GBA BIOS HLE FUNCTIONS
    // ---------------------------------------------------------

    TInt ExecuteBiosSoftReset(TNgbaCpuState& cpu)
    {
        for (TUint32 addr = 0x03007E00u; addr < 0x03008000u; addr += 4u)
            NgbaMemoryWrite32(addr, 0);

        const TUint8 flag = NgbaMemoryRead8(0x03007FFAu);
        const TUint32 target = (flag != 0) ? 0x02000000u : KRomBase;

        for (TInt i = 0; i < 15; ++i)
            cpu.r[i] = 0;

        gBank.r13_usr = 0x03007F00u;
        gBank.r14_usr = 0;
        gBank.r13_irq = 0x03007FA0u;
        gBank.r14_irq = 0;
        gBank.r13_svc = 0x03007FE0u;
        gBank.r14_svc = 0;
        gBank.intrWaitMask = 0;
        gBank.irqTrampolineActive = EFalse;

        cpu.cpsr = 0x000000DFu;
        cpu.r[13] = 0x03007F00u;
        cpu.r[15] = target;
        cpu.halted = EFalse;
        SetThumbState(cpu, EFalse);

        return ENgbaCpuOk;
    }

    TInt ExecuteBiosRegisterRamReset(TNgbaCpuState& cpu)
    {
        NgbaMemoryRegisterRamReset(cpu.r[0]);
        return ENgbaCpuOk;
    }

    TInt ExecuteBiosHalt(TNgbaCpuState& cpu)
    {
        cpu.halted = ETrue;
        return ENgbaCpuOk;
    }

    TInt ExecuteBiosIntrWait(
        TNgbaCpuState& cpu,
        TUint32 address,
        TBool vblankDefault
    )
    {
        if (gBank.intrWaitMask == 0)
        {
            const TBool discardOld = vblankDefault
                ? ETrue
                : (cpu.r[0] != 0);

            const TUint16 mask = vblankDefault
                ? 0x0001u
                : static_cast<TUint16>(cpu.r[1] & 0xFFFFu);

            if (mask == 0)
                return ENgbaCpuOk;

            if (discardOld)
            {
                const TUint16 biosIf = NgbaMemoryRead16(0x03007FF8u);
                NgbaMemoryWrite16(
                    0x03007FF8u,
                    static_cast<TUint16>(biosIf & ~mask)
                );
            }

            gBank.intrWaitMask = mask;
        }

        const TUint16 biosIf = NgbaMemoryRead16(0x03007FF8u);

        if ((biosIf & gBank.intrWaitMask) != 0)
        {
            NgbaMemoryWrite16(
                0x03007FF8u,
                static_cast<TUint16>(biosIf & ~gBank.intrWaitMask)
            );
            gBank.intrWaitMask = 0;
            return ENgbaCpuOk;
        }

        cpu.r[15] = address;
        cpu.halted = ETrue;
        return ENgbaCpuOk;
    }

    TInt ExecuteBiosDiv(TNgbaCpuState& cpu, TBool armOrder)
    {
        const TInt32 numerator = armOrder
            ? static_cast<TInt32>(cpu.r[1])
            : static_cast<TInt32>(cpu.r[0]);

        const TInt32 denominator = armOrder
            ? static_cast<TInt32>(cpu.r[0])
            : static_cast<TInt32>(cpu.r[1]);

        if (denominator == 0)
        {
            cpu.r[0] = (numerator < 0) ? 0xFFFFFFFFu : 1u;
            cpu.r[1] = static_cast<TUint32>(numerator);
            cpu.r[3] = 1u;
            return ENgbaCpuOk;
        }

        if (numerator == (-2147483647 - 1) && denominator == -1)
        {
            cpu.r[0] = 0x80000000u;
            cpu.r[1] = 0u;
            cpu.r[3] = 0x80000000u;
            return ENgbaCpuOk;
        }

        const TInt32 quotient = numerator / denominator;
        const TInt32 remainder = numerator % denominator;

        const TInt64 q64 = static_cast<TInt64>(quotient);
        const TUint32 absQuotient = (q64 < 0)
            ? static_cast<TUint32>(-q64)
            : static_cast<TUint32>(q64);

        cpu.r[0] = static_cast<TUint32>(quotient);
        cpu.r[1] = static_cast<TUint32>(remainder);
        cpu.r[3] = absQuotient;

        return ENgbaCpuOk;
    }

    TInt ExecuteBiosSqrt(TNgbaCpuState& cpu)
    {
        TUint32 value = cpu.r[0];
        TUint32 result = 0;
        TUint32 bit = 1u << 30;

        while (bit > value)
            bit >>= 2;

        while (bit != 0)
        {
            if (value >= result + bit)
            {
                value -= result + bit;
                result = (result >> 1) + bit;
            }
            else
            {
                result >>= 1;
            }

            bit >>= 2;
        }

        cpu.r[0] = result;
        return ENgbaCpuOk;
    }

    TInt32 ComputeBiosArcTan(TInt16 tanVal)
    {
        const TInt32 i = static_cast<TInt32>(tanVal);
        const TInt32 a = -(i * i) >> 14;
        TInt32 b = ((0x00A9 * a) >> 14) + 0x0390;
        b = ((b * a) >> 14) + 0x091C;
        b = ((b * a) >> 14) + 0x0FB6;
        b = ((b * a) >> 14) + 0x16AA;
        b = ((b * a) >> 14) + 0x2081;
        b = ((b * a) >> 14) + 0x3651;
        b = ((b * a) >> 14) + 0xA2F9;
        return (i * b) >> 16;
    }

    TInt ExecuteBiosArcTan(TNgbaCpuState& cpu)
    {
        const TInt16 tanVal = static_cast<TInt16>(cpu.r[0] & 0xFFFFu);
        cpu.r[0] = static_cast<TUint32>(ComputeBiosArcTan(tanVal));
        return ENgbaCpuOk;
    }

    TInt ExecuteBiosArcTan2(TNgbaCpuState& cpu)
    {
        const TInt32 x = static_cast<TInt16>(cpu.r[0] & 0xFFFFu);
        const TInt32 y = static_cast<TInt16>(cpu.r[1] & 0xFFFFu);
        TUint32 result = 0;

        if (y == 0)
        {
            result = (x >= 0) ? 0u : 0x8000u;
        }
        else if (x == 0)
        {
            result = (y >= 0) ? 0x4000u : 0xC000u;
        }
        else
        {
            const TInt32 absX = (x < 0) ? -x : x;
            const TInt32 absY = (y < 0) ? -y : y;

            if (absY <= absX)
            {
                const TInt16 ratio = static_cast<TInt16>((y << 14) / x);
                const TInt32 angle = ComputeBiosArcTan(ratio);
                result = (x >= 0)
                    ? static_cast<TUint32>(angle & 0xFFFF)
                    : static_cast<TUint32>((0x8000 + angle) & 0xFFFF);
            }
            else
            {
                const TInt16 ratio = static_cast<TInt16>((x << 14) / y);
                const TInt32 angle = ComputeBiosArcTan(ratio);
                result = (y >= 0)
                    ? static_cast<TUint32>((0x4000 - angle) & 0xFFFF)
                    : static_cast<TUint32>((0xC000 - angle) & 0xFFFF);
            }
        }

        cpu.r[0] = result;
        return ENgbaCpuOk;
    }

    TInt ExecuteBiosCpuSet(TNgbaCpuState& cpu)
    {
        const TUint32 source = cpu.r[0];
        const TUint32 destination = cpu.r[1];
        const TUint32 control = cpu.r[2];

        const TBool wordMode =
            (control & (1u << 24)) != 0;

        const TBool fixedSource =
            (control & (1u << 26)) != 0;

        const TUint32 count =
            control & 0x001FFFFFu;

        if (count == 0)
            return ENgbaCpuOk;

        if (wordMode)
        {
            TUint32 src = source & ~3u;
            TUint32 dst = destination & ~3u;
            TUint32 value = 0;

            if (fixedSource)
                value = NgbaMemoryRead32(src);

            for (TUint32 i = 0; i < count; ++i)
            {
                if (!fixedSource)
                    value = NgbaMemoryRead32(src);

                NgbaMemoryWrite32(dst, value);

                if (!fixedSource)
                    src += 4;

                dst += 4;
            }
        }
        else
        {
            TUint32 src = source & ~1u;
            TUint32 dst = destination & ~1u;
            TUint16 value = 0;

            if (fixedSource)
                value = NgbaMemoryRead16(src);

            for (TUint32 i = 0; i < count; ++i)
            {
                if (!fixedSource)
                    value = NgbaMemoryRead16(src);

                NgbaMemoryWrite16(dst, value);

                if (!fixedSource)
                    src += 2;

                dst += 2;
            }
        }

        return ENgbaCpuOk;
    }

    TInt ExecuteBiosCpuFastSet(TNgbaCpuState& cpu)
    {
        TUint32 source = cpu.r[0] & ~3u;
        TUint32 destination = cpu.r[1] & ~3u;
        const TUint32 control = cpu.r[2];

        const TBool fixedSource =
            (control & (1u << 24)) != 0;

        TUint32 count = control & 0x001FFFFFu;

        count = (count + 7u) & ~7u;

        if (count == 0)
            return ENgbaCpuOk;

        TUint32 fillValue = 0;

        if (fixedSource)
            fillValue = NgbaMemoryRead32(source);

        for (TUint32 i = 0; i < count; ++i)
        {
            TUint32 value = fillValue;

            if (!fixedSource)
            {
                value = NgbaMemoryRead32(source);
                source += 4;
            }

            NgbaMemoryWrite32(destination, value);
            destination += 4;
        }

        return ENgbaCpuOk;
    }

    TInt ExecuteBiosBgAffineSet(TNgbaCpuState& cpu)
    {
        TUint32 src = cpu.r[0];
        TUint32 dst = cpu.r[1];
        const TUint32 count = cpu.r[2];

        for (TUint32 i = 0; i < count; ++i)
        {
            const TInt32 bgX = static_cast<TInt32>(NgbaMemoryRead32(src + 0u));
            const TInt32 bgY = static_cast<TInt32>(NgbaMemoryRead32(src + 4u));
            const TInt32 scrX = static_cast<TInt16>(NgbaMemoryRead16(src + 8u));
            const TInt32 scrY = static_cast<TInt16>(NgbaMemoryRead16(src + 10u));
            const TInt32 scaleX = static_cast<TInt16>(NgbaMemoryRead16(src + 12u));
            const TInt32 scaleY = static_cast<TInt16>(NgbaMemoryRead16(src + 14u));
            const TUint16 angle = NgbaMemoryRead16(src + 16u);
            src += 20u;

            const TInt32 cosVal = FixedCos8_8(angle);
            const TInt32 sinVal = FixedSin8_8(angle);

            const TInt16 pa = static_cast<TInt16>((cosVal * scaleX) >> 8);
            const TInt16 pb = static_cast<TInt16>((-sinVal * scaleX) >> 8);
            const TInt16 pc = static_cast<TInt16>((sinVal * scaleY) >> 8);
            const TInt16 pd = static_cast<TInt16>((cosVal * scaleY) >> 8);

            const TInt32 startX = bgX - (pa * scrX + pb * scrY);
            const TInt32 startY = bgY - (pc * scrX + pd * scrY);

            NgbaMemoryWrite16(dst + 0u, static_cast<TUint16>(pa));
            NgbaMemoryWrite16(dst + 2u, static_cast<TUint16>(pb));
            NgbaMemoryWrite16(dst + 4u, static_cast<TUint16>(pc));
            NgbaMemoryWrite16(dst + 6u, static_cast<TUint16>(pd));
            NgbaMemoryWrite32(dst + 8u, static_cast<TUint32>(startX));
            NgbaMemoryWrite32(dst + 12u, static_cast<TUint32>(startY));
            dst += 16u;
        }

        return ENgbaCpuOk;
    }

    TInt ExecuteBiosObjAffineSet(TNgbaCpuState& cpu)
    {
        TUint32 src = cpu.r[0];
        TUint32 dst = cpu.r[1];
        const TUint32 count = cpu.r[2];
        const TUint32 stride = cpu.r[3];

        for (TUint32 i = 0; i < count; ++i)
        {
            const TInt32 scaleX = static_cast<TInt16>(NgbaMemoryRead16(src + 0u));
            const TInt32 scaleY = static_cast<TInt16>(NgbaMemoryRead16(src + 2u));
            const TUint16 angle = NgbaMemoryRead16(src + 4u);
            src += 8u;

            const TInt32 cosVal = FixedCos8_8(angle);
            const TInt32 sinVal = FixedSin8_8(angle);

            const TInt16 pa = static_cast<TInt16>((cosVal * scaleX) >> 8);
            const TInt16 pb = static_cast<TInt16>((-sinVal * scaleX) >> 8);
            const TInt16 pc = static_cast<TInt16>((sinVal * scaleY) >> 8);
            const TInt16 pd = static_cast<TInt16>((cosVal * scaleY) >> 8);

            NgbaMemoryWrite16(dst + stride * 0u, static_cast<TUint16>(pa));
            NgbaMemoryWrite16(dst + stride * 1u, static_cast<TUint16>(pb));
            NgbaMemoryWrite16(dst + stride * 2u, static_cast<TUint16>(pc));
            NgbaMemoryWrite16(dst + stride * 3u, static_cast<TUint16>(pd));
            dst += stride * 4u;
        }

        return ENgbaCpuOk;
    }

    TInt ExecuteBiosBitUnPack(TNgbaCpuState& cpu)
    {
        TUint32 src = cpu.r[0];
        TUint32 dst = cpu.r[1] & ~3u;
        const TUint32 paramAddr = cpu.r[2];

        const TUint16 srcLen = NgbaMemoryRead16(paramAddr + 0u);
        const TUint8 srcWidth = NgbaMemoryRead8(paramAddr + 2u);
        const TUint8 dstWidth = NgbaMemoryRead8(paramAddr + 3u);
        const TUint32 dataOffsetField = NgbaMemoryRead32(paramAddr + 4u);

        if (srcWidth == 0 || dstWidth == 0)
            return ENgbaCpuOk;

        const TUint32 dataOffset = dataOffsetField & 0x7FFFFFFFu;
        const TBool zeroDataFlag = (dataOffsetField & 0x80000000u) != 0;
        const TUint32 srcMask = (srcWidth >= 32)
            ? 0xFFFFFFFFu
            : ((1u << srcWidth) - 1u);

        TUint32 outWord = 0;
        TInt outBitPos = 0;

        for (TUint32 i = 0; i < srcLen; ++i)
        {
            const TUint8 inByte = NgbaMemoryRead8(src + i);

            for (TInt bitPos = 0; bitPos < 8; bitPos += srcWidth)
            {
                TUint32 val = (static_cast<TUint32>(inByte) >> bitPos) & srcMask;

                if (val != 0 || zeroDataFlag)
                    val += dataOffset;

                outWord |= (val << outBitPos);
                outBitPos += dstWidth;

                if (outBitPos >= 32)
                {
                    NgbaMemoryWrite32(dst, outWord);
                    dst += 4u;
                    outWord = 0;
                    outBitPos = 0;
                }
            }
        }

        if (outBitPos > 0)
            NgbaMemoryWrite32(dst, outWord);

        return ENgbaCpuOk;
    }

    TInt ExecuteBiosLz77UnComp(TNgbaCpuState& cpu, TBool write16Bit)
    {
        TUint32 src = cpu.r[0];
        TUint32 dst = cpu.r[1];

        const TUint32 header = NgbaMemoryRead32(src);
        src += 4u;

        const TUint32 destSize = header >> 8;
        TUint32 written = 0;
        TUint16 halfwordBuffer = 0;

        while (written < destSize)
        {
            const TUint8 flags = NgbaMemoryRead8(src++);

            for (TInt bit = 7; bit >= 0 && written < destSize; --bit)
            {
                if ((flags & (1u << bit)) == 0)
                {
                    const TUint8 value = NgbaMemoryRead8(src++);

                    if (write16Bit)
                    {
                        if ((written & 1u) == 0)
                        {
                            halfwordBuffer = value;
                        }
                        else
                        {
                            halfwordBuffer |=
                                static_cast<TUint16>(value) << 8;
                            NgbaMemoryWrite16(dst, halfwordBuffer);
                            dst += 2u;
                        }
                    }
                    else
                    {
                        NgbaMemoryWrite8(dst++, value);
                    }

                    ++written;
                }
                else
                {
                    const TUint8 b1 = NgbaMemoryRead8(src++);
                    const TUint8 b2 = NgbaMemoryRead8(src++);

                    const TUint32 length =
                        ((static_cast<TUint32>(b1) >> 4) & 0xFu) + 3u;
                    const TUint32 disp =
                        (((static_cast<TUint32>(b1) & 0xFu) << 8) |
                         static_cast<TUint32>(b2)) + 1u;

                    for (TUint32 k = 0; k < length && written < destSize; ++k)
                    {
                        TUint8 value = 0;

                        if (write16Bit)
                        {
                            const TUint32 currentOutAddr =
                                cpu.r[1] + written;
                            const TUint32 refAddr =
                                currentOutAddr - disp;

                            if (refAddr == currentOutAddr - 1u &&
                                (written & 1u) != 0)
                            {
                                value = static_cast<TUint8>(
                                    halfwordBuffer & 0xFFu
                                );
                            }
                            else
                            {
                                value = NgbaMemoryRead8(refAddr);
                            }

                            if ((written & 1u) == 0)
                            {
                                halfwordBuffer = value;
                            }
                            else
                            {
                                halfwordBuffer |=
                                    static_cast<TUint16>(value) << 8;
                                NgbaMemoryWrite16(dst, halfwordBuffer);
                                dst += 2u;
                            }
                        }
                        else
                        {
                            value = NgbaMemoryRead8(dst - disp);
                            NgbaMemoryWrite8(dst++, value);
                        }

                        ++written;
                    }
                }
            }
        }

        if (write16Bit && (written & 1u) != 0)
            NgbaMemoryWrite16(dst, halfwordBuffer);

        return ENgbaCpuOk;
    }

    TInt ExecuteBiosHuffUnComp(TNgbaCpuState& cpu)
    {
        TUint32 src = cpu.r[0];
        TUint32 dst = cpu.r[1] & ~3u;

        const TUint32 header = NgbaMemoryRead32(src);
        const TUint32 bitDepth = header & 0x0Fu;
        const TUint32 destSize = header >> 8;

        if (bitDepth != 4u && bitDepth != 8u)
            return ENgbaCpuOk;

        const TUint32 treeSize =
            (static_cast<TUint32>(NgbaMemoryRead8(src + 4u)) + 1u) * 2u;
        const TUint32 treeRootAddr = src + 5u;
        TUint32 bitStreamAddr = src + 4u + treeSize;

        TUint32 nodeAddr = treeRootAddr;
        TUint32 outWord = 0;
        TInt outShift = 0;
        TUint32 written = 0;

        while (written < destSize)
        {
            const TUint32 bits = NgbaMemoryRead32(bitStreamAddr);
            bitStreamAddr += 4u;

            for (TInt i = 31; i >= 0 && written < destSize; --i)
            {
                const TUint8 node = NgbaMemoryRead8(nodeAddr);
                const TUint32 childOffset =
                    ((static_cast<TUint32>(node) & 0x3Fu) + 1u) * 2u;
                const TUint32 nextNodeAddr =
                    (nodeAddr & ~1u) + childOffset + ((bits >> i) & 1u);

                const TBool isLeaf = ((bits >> i) & 1u)
                    ? ((node & 0x40u) != 0)
                    : ((node & 0x80u) != 0);

                if (isLeaf)
                {
                    const TUint32 symbol = NgbaMemoryRead8(nextNodeAddr);
                    outWord |= (symbol << outShift);
                    outShift += static_cast<TInt>(bitDepth);

                    if (outShift >= 32)
                    {
                        NgbaMemoryWrite32(dst, outWord);
                        dst += 4u;
                        written += 4u;
                        outWord = 0;
                        outShift = 0;
                    }

                    nodeAddr = treeRootAddr;
                }
                else
                {
                    nodeAddr = nextNodeAddr;
                }
            }
        }

        return ENgbaCpuOk;
    }

    TInt ExecuteBiosRlUnComp(TNgbaCpuState& cpu, TBool write16Bit)
    {
        TUint32 src = cpu.r[0];
        TUint32 dst = cpu.r[1];

        const TUint32 header = NgbaMemoryRead32(src);
        src += 4u;

        const TUint32 destSize = header >> 8;
        TUint32 written = 0;
        TUint16 halfwordBuffer = 0;

        while (written < destSize)
        {
            const TUint8 flag = NgbaMemoryRead8(src++);

            if ((flag & 0x80u) == 0)
            {
                const TUint32 length =
                    (static_cast<TUint32>(flag) & 0x7Fu) + 1u;

                for (TUint32 i = 0; i < length && written < destSize; ++i)
                {
                    const TUint8 value = NgbaMemoryRead8(src++);

                    if (write16Bit)
                    {
                        if ((written & 1u) == 0)
                        {
                            halfwordBuffer = value;
                        }
                        else
                        {
                            halfwordBuffer |=
                                static_cast<TUint16>(value) << 8;
                            NgbaMemoryWrite16(dst, halfwordBuffer);
                            dst += 2u;
                        }
                    }
                    else
                    {
                        NgbaMemoryWrite8(dst++, value);
                    }

                    ++written;
                }
            }
            else
            {
                const TUint32 length =
                    (static_cast<TUint32>(flag) & 0x7Fu) + 3u;
                const TUint8 value = NgbaMemoryRead8(src++);

                for (TUint32 i = 0; i < length && written < destSize; ++i)
                {
                    if (write16Bit)
                    {
                        if ((written & 1u) == 0)
                        {
                            halfwordBuffer = value;
                        }
                        else
                        {
                            halfwordBuffer |=
                                static_cast<TUint16>(value) << 8;
                            NgbaMemoryWrite16(dst, halfwordBuffer);
                            dst += 2u;
                        }
                    }
                    else
                    {
                        NgbaMemoryWrite8(dst++, value);
                    }

                    ++written;
                }
            }
        }

        if (write16Bit && (written & 1u) != 0)
            NgbaMemoryWrite16(dst, halfwordBuffer);

        return ENgbaCpuOk;
    }

    TInt ExecuteBiosDiffUnFilter8(TNgbaCpuState& cpu, TBool write16Bit)
    {
        TUint32 src = cpu.r[0];
        TUint32 dst = cpu.r[1];

        const TUint32 header = NgbaMemoryRead32(src);
        src += 4u;

        const TUint32 destSize = header >> 8;
        TUint8 current = 0;
        TUint16 halfwordBuffer = 0;

        for (TUint32 i = 0; i < destSize; ++i)
        {
            current = static_cast<TUint8>(
                current + NgbaMemoryRead8(src++)
            );

            if (write16Bit)
            {
                if ((i & 1u) == 0)
                {
                    halfwordBuffer = current;
                }
                else
                {
                    halfwordBuffer |=
                        static_cast<TUint16>(current) << 8;
                    NgbaMemoryWrite16(dst, halfwordBuffer);
                    dst += 2u;
                }
            }
            else
            {
                NgbaMemoryWrite8(dst++, current);
            }
        }

        return ENgbaCpuOk;
    }

    TInt ExecuteBiosDiffUnFilter16(TNgbaCpuState& cpu)
    {
        TUint32 src = cpu.r[0];
        TUint32 dst = cpu.r[1];

        const TUint32 header = NgbaMemoryRead32(src);
        src += 4u;

        const TUint32 destSize = header >> 8;
        TUint16 current = 0;

        for (TUint32 i = 0; i + 1u < destSize; i += 2u)
        {
            current = static_cast<TUint16>(
                current + NgbaMemoryRead16(src)
            );
            src += 2u;
            NgbaMemoryWrite16(dst, current);
            dst += 2u;
        }

        return ENgbaCpuOk;
    }

    TInt ExecuteBiosSoundBias(TNgbaCpuState& cpu)
    {
        const TUint16 oldBias = NgbaMemoryRead16(0x04000088u);
        const TUint16 level = (cpu.r[0] != 0) ? 0x0200u : 0x0000u;
        NgbaMemoryWrite16(
            0x04000088u,
            static_cast<TUint16>((oldBias & ~0x03FEu) | level)
        );
        return ENgbaCpuOk;
    }

    TInt ExecuteBiosMidiKey2Freq(TNgbaCpuState& cpu)
    {
        const TUint32 baseFreq = NgbaMemoryRead32(cpu.r[0] + 4u);
        const TInt32 mk = static_cast<TInt32>(cpu.r[1] & 0xFFu);
        const TInt32 fp = static_cast<TInt32>(cpu.r[2] & 0xFFu);

        const TInt32 diff = ((180 - mk) << 8) - fp;
        TInt32 octaves = diff / 3072;
        TInt32 rem = diff % 3072;

        if (rem < 0)
        {
            rem += 3072;
            --octaves;
        }

        const TInt32 semitone = rem >> 8;
        const TUint32 fine = static_cast<TUint32>(rem & 0xFF);

        const TUint32 f0 = KSemitoneInvTable[semitone];
        const TUint32 f1 = KSemitoneInvTable[semitone + 1];
        const TUint32 interp = f0 - (((f0 - f1) * fine) >> 8);

        TUint64 scaled =
            (static_cast<TUint64>(baseFreq) * static_cast<TUint64>(interp)) >> 16;

        if (octaves > 0)
            scaled >>= octaves;
        else if (octaves < 0)
            scaled <<= (-octaves);

        cpu.r[0] = static_cast<TUint32>(scaled & 0xFFFFFFFFu);
        return ENgbaCpuOk;
    }

    TInt ExecuteBiosSwi(
        TNgbaCpuState& cpu,
        TUint32 swiNumber,
        TUint32 address
    )
    {
        switch (swiNumber)
        {
        case 0x00u:
            return ExecuteBiosSoftReset(cpu);

        case 0x01u:
            return ExecuteBiosRegisterRamReset(cpu);

        case 0x02u:
        case 0x03u:
            return ExecuteBiosHalt(cpu);

        case 0x04u:
            return ExecuteBiosIntrWait(cpu, address, EFalse);

        case 0x05u:
            return ExecuteBiosIntrWait(cpu, address, ETrue);

        case 0x06u:
            return ExecuteBiosDiv(cpu, EFalse);

        case 0x07u:
            return ExecuteBiosDiv(cpu, ETrue);

        case 0x08u:
            return ExecuteBiosSqrt(cpu);

        case 0x09u:
            return ExecuteBiosArcTan(cpu);

        case 0x0Au:
            return ExecuteBiosArcTan2(cpu);

        case 0x0Bu:
            return ExecuteBiosCpuSet(cpu);

        case 0x0Cu:
            return ExecuteBiosCpuFastSet(cpu);

        case 0x0Du:
            cpu.r[0] = 0xBAAE187Fu;
            return ENgbaCpuOk;
            
        case 0x0Eu:
            return ExecuteBiosBgAffineSet(cpu);

        case 0x0Fu:
            return ExecuteBiosObjAffineSet(cpu);

        case 0x10u:
            return ExecuteBiosBitUnPack(cpu);

        case 0x11u:
            return ExecuteBiosLz77UnComp(cpu, EFalse);

        case 0x12u:
            return ExecuteBiosLz77UnComp(cpu, ETrue);

        case 0x13u:
            return ExecuteBiosHuffUnComp(cpu);

        case 0x14u:
            return ExecuteBiosRlUnComp(cpu, EFalse);

        case 0x15u:
            return ExecuteBiosRlUnComp(cpu, ETrue);

        case 0x16u:
            return ExecuteBiosDiffUnFilter8(cpu, EFalse);

        case 0x17u:
            return ExecuteBiosDiffUnFilter8(cpu, ETrue);

        case 0x18u:
            return ExecuteBiosDiffUnFilter16(cpu);

        case 0x19u:
            return ExecuteBiosSoundBias(cpu);

        case 0x1Fu:
            return ExecuteBiosMidiKey2Freq(cpu);

        default:
            return ENgbaCpuUnsupported;
        }
    }

    // ---------------------------------------------------------
    // ARM INSTRUCTIONS
    // ---------------------------------------------------------

    TInt ExecuteMultiply(
        TNgbaCpuState& cpu,
        TUint32 instruction
    )
    {
        const TBool accumulate =
            (instruction & (1u << 21)) != 0;

        const TBool setFlags =
            (instruction & (1u << 20)) != 0;

        const TInt rd = (instruction >> 16) & 15;
        const TInt rn = (instruction >> 12) & 15;
        const TInt rs = (instruction >> 8) & 15;
        const TInt rm = instruction & 15;

        if (rd == 15 || rm == 15 || rs == 15)
            return ENgbaCpuUnsupported;

        if (accumulate && rn == 15)
            return ENgbaCpuUnsupported;

        TUint32 result = cpu.r[rm] * cpu.r[rs];

        if (accumulate)
            result += cpu.r[rn];

        cpu.r[rd] = result;

        if (setFlags)
            SetNzFlags(cpu, result);

        return ENgbaCpuOk;
    }

    TInt ExecuteMultiplyLong(
        TNgbaCpuState& cpu,
        TUint32 instruction
    )
    {
        const TBool isSigned =
            (instruction & (1u << 22)) != 0;

        const TBool accumulate =
            (instruction & (1u << 21)) != 0;

        const TBool setFlags =
            (instruction & (1u << 20)) != 0;

        const TInt rdHi = (instruction >> 16) & 15;
        const TInt rdLo = (instruction >> 12) & 15;
        const TInt rs = (instruction >> 8) & 15;
        const TInt rm = instruction & 15;

        if (rdHi == 15 || rdLo == 15 ||
            rs == 15 || rm == 15 || rdHi == rdLo)
        {
            return ENgbaCpuUnsupported;
        }

        TUint64 result = 0;

        if (isSigned)
        {
            const TInt64 a =
                static_cast<TInt64>(static_cast<TInt32>(cpu.r[rm]));
            const TInt64 b =
                static_cast<TInt64>(static_cast<TInt32>(cpu.r[rs]));
            result = static_cast<TUint64>(a * b);
        }
        else
        {
            const TUint64 a = static_cast<TUint64>(cpu.r[rm]);
            const TUint64 b = static_cast<TUint64>(cpu.r[rs]);
            result = a * b;
        }

        if (accumulate)
        {
            const TUint64 acc =
                (static_cast<TUint64>(cpu.r[rdHi]) << 32) |
                static_cast<TUint64>(cpu.r[rdLo]);
            result += acc;
        }

        cpu.r[rdLo] = static_cast<TUint32>(result & 0xFFFFFFFFu);
        cpu.r[rdHi] = static_cast<TUint32>((result >> 32) & 0xFFFFFFFFu);

        if (setFlags)
        {
            cpu.cpsr &= ~(KFlagN | KFlagZ);

            if ((result >> 63) & 1u)
                cpu.cpsr |= KFlagN;

            if (result == 0)
                cpu.cpsr |= KFlagZ;
        }

        return ENgbaCpuOk;
    }

    TInt ExecuteSwap(
        TNgbaCpuState& cpu,
        TUint32 instruction
    )
    {
        const TBool byte = (instruction & (1u << 22)) != 0;
        const TInt rn = (instruction >> 16) & 15;
        const TInt rd = (instruction >> 12) & 15;
        const TInt rm = instruction & 15;

        if (rn == 15 || rd == 15 || rm == 15)
            return ENgbaCpuUnsupported;

        const TUint32 memoryAddress = cpu.r[rn];
        const TUint32 writeValue = cpu.r[rm];

        if (byte)
        {
            const TUint32 readValue = NgbaMemoryRead8(memoryAddress);
            NgbaMemoryWrite8(
                memoryAddress,
                static_cast<TUint8>(writeValue)
            );
            cpu.r[rd] = readValue;
        }
        else
        {
            const TUint32 readValue = ReadWordRotated(memoryAddress);
            NgbaMemoryWrite32(memoryAddress & ~3u, writeValue);
            cpu.r[rd] = readValue;
        }

        return ENgbaCpuOk;
    }

    TInt ExecuteMsr(
        TNgbaCpuState& cpu,
        TUint32 instruction,
        TUint32 operand
    )
    {
        const TBool useSpsr = (instruction & (1u << 22)) != 0;
        TUint32 writeMask = 0;

        if (instruction & (1u << 16))
            writeMask |= 0x000000FFu;
        if (instruction & (1u << 17))
            writeMask |= 0x0000FF00u;
        if (instruction & (1u << 18))
            writeMask |= 0x00FF0000u;
        if (instruction & (1u << 19))
            writeMask |= 0xFF000000u;

        if (!useSpsr)
        {
            if ((cpu.cpsr & KModeMask) == KModeUser)
                writeMask &= 0xFF000000u;

            // MSR tidak mengubah bit Thumb (T) secara langsung pada ARM7TDMI.
            writeMask &= ~KFlagT;

            const TUint32 updated =
                (cpu.cpsr & ~writeMask) | (operand & writeMask);
            SetCpsrWithMode(cpu, updated);
        }
        else
        {
            const TUint32 oldSpsr = GetCurrentSpsr(cpu);
            const TUint32 updated =
                (oldSpsr & ~writeMask) | (operand & writeMask);
            SetCurrentSpsr(cpu, updated);
        }

        return ENgbaCpuOk;
    }

    TInt ExecuteHalfwordTransfer(
        TNgbaCpuState& cpu,
        TUint32 instruction,
        TUint32 address
    )
    {
        const TBool pre = (instruction & (1u << 24)) != 0;
        const TBool add = (instruction & (1u << 23)) != 0;
        const TBool immediate = (instruction & (1u << 22)) != 0;
        const TBool writeBack = (instruction & (1u << 21)) != 0;
        const TBool load = (instruction & (1u << 20)) != 0;

        const TInt rn = (instruction >> 16) & 15;
        const TInt rd = (instruction >> 12) & 15;
        const TInt operation = (instruction >> 5) & 3;

        if (operation == 0)
            return ENgbaCpuUnsupported;

        if (!load && operation != 1)
            return ENgbaCpuUnsupported;

        TUint32 offset = 0;

        if (immediate)
        {
            offset =
                ((instruction >> 4) & 0xF0u) |
                (instruction & 15u);
        }
        else
        {
            offset = ReadRegister(
                cpu,
                instruction & 15,
                address
            );
        }

        const TUint32 base = ReadRegister(cpu, rn, address);
        const TUint32 adjusted = add ? base + offset : base - offset;
        const TUint32 memoryAddress = pre ? adjusted : base;

        if (load)
        {
            TUint32 value = 0;

            if (operation == 1)
            {
                value = ReadHalfwordGba(memoryAddress);
            }
            else if (operation == 2)
            {
                value = static_cast<TUint32>(
                    static_cast<TInt32>(
                        static_cast<TInt8>(
                            NgbaMemoryRead8(memoryAddress)
                        )
                    )
                );
            }
            else
            {
                if (memoryAddress & 1u)
                {
                    value = static_cast<TUint32>(
                        static_cast<TInt32>(
                            static_cast<TInt8>(
                                NgbaMemoryRead8(memoryAddress)
                            )
                        )
                    );
                }
                else
                {
                    value = static_cast<TUint32>(
                        static_cast<TInt32>(
                            static_cast<TInt16>(
                                NgbaMemoryRead16(memoryAddress)
                            )
                        )
                    );
                }
            }

            if (!pre || writeBack)
            {
                if (rn != 15)
                    cpu.r[rn] = adjusted;
            }

            if (rd == 15)
            {
                SetThumbState(cpu, EFalse);
                cpu.r[15] = value & ~3u;
            }
            else
            {
                cpu.r[rd] = value;
            }
        }
        else
        {
            NgbaMemoryWrite16(
                memoryAddress & ~1u,
                static_cast<TUint16>(
                    ReadRegister(cpu, rd, address)
                )
            );

            if (!pre || writeBack)
            {
                if (rn != 15)
                    cpu.r[rn] = adjusted;
            }
        }

        return ENgbaCpuOk;
    }

    TInt ExecuteBlockTransfer(
        TNgbaCpuState& cpu,
        TUint32 instruction,
        TUint32 address
    )
    {
        const TBool pre = (instruction & (1u << 24)) != 0;
        const TBool add = (instruction & (1u << 23)) != 0;
        const TBool userMode = (instruction & (1u << 22)) != 0;
        const TBool writeBack = (instruction & (1u << 21)) != 0;
        const TBool load = (instruction & (1u << 20)) != 0;

        const TInt rn = (instruction >> 16) & 15;
        TUint32 list = instruction & 0xFFFFu;

        const TBool emptyList = (list == 0);

        if (emptyList)
            list = (1u << 15);

        const TUint32 base = ReadRegister(cpu, rn, address);

        TInt count = 0;

        if (emptyList)
        {
            count = 16;
        }
        else
        {
            for (TInt i = 0; i < 16; ++i)
                if (list & (1u << i))
                    ++count;
        }

        TUint32 memoryAddress = base;

        if (add)
            memoryAddress = base + (pre ? 4u : 0u);
        else
            memoryAddress = base - (count * 4u) +
                            (pre ? 0u : 4u);

        const TUint32 finalBase =
            add ? base + count * 4u : base - count * 4u;

        const TBool restoreSpsr =
            load && userMode && ((list & (1u << 15)) != 0);

        const TBool transferUserBank =
            userMode && !restoreSpsr;

        TInt firstReg = -1;

        for (TInt i = 0; i < 16; ++i)
        {
            if (list & (1u << i))
            {
                firstReg = i;
                break;
            }
        }

        for (TInt i = 0; i < 16; ++i)
        {
            if (!(list & (1u << i)))
                continue;

            if (load)
            {
                const TUint32 value =
                    NgbaMemoryRead32(memoryAddress);

                if (i == 15)
                {
                    if (restoreSpsr)
                    {
                        const TUint32 spsr = GetCurrentSpsr(cpu);
                        SetCpsrWithMode(cpu, spsr);
                        cpu.r[15] = cpu.thumb
                            ? (value & ~1u)
                            : (value & ~3u);
                    }
                    else
                    {
                        SetThumbState(cpu, EFalse);
                        cpu.r[15] = value & ~3u;
                    }
                }
                else if (transferUserBank)
                {
                    WriteUserRegister(cpu, i, value);
                }
                else
                {
                    cpu.r[i] = value;
                }
            }
            else
            {
                TUint32 value = 0;

                if (i == rn && writeBack && i != firstReg)
                    value = finalBase;
                else if (transferUserBank && i != 15)
                    value = ReadUserRegister(cpu, i);
                else
                    value = ReadRegister(cpu, i, address);

                NgbaMemoryWrite32(memoryAddress, value);
            }

            memoryAddress += 4u;
        }

        if (writeBack && rn != 15)
        {
            if (!(load && (list & (1u << rn))))
                cpu.r[rn] = finalBase;
        }

        return ENgbaCpuOk;
    }

    TInt ExecuteArm(
        TNgbaCpuState& cpu,
        TUint32 instruction,
        TUint32 address
    )
    {
        const TUint32 condition = instruction >> 28;

        if (condition == 0xFu)
            return ENgbaCpuUnsupported;

        if (!ConditionPassed(cpu.cpsr, condition))
            return ENgbaCpuOk;

        // BX: perpindahan ARM / Thumb.
        if ((instruction & 0x0FFFFFF0u) == 0x012FFF10u)
        {
            const TUint32 target =
                ReadRegister(cpu, instruction & 15, address);

            const TBool thumb = (target & 1u) != 0;

            SetThumbState(cpu, thumb);

            cpu.r[15] = thumb
                ? (target & ~1u)
                : (target & ~3u);

            return ENgbaCpuOk;
        }

        // MRS: baca CPSR atau SPSR ke register.
        if ((instruction & 0x0FBF0FFFu) == 0x010F0000u)
        {
            const TBool useSpsr = (instruction & (1u << 22)) != 0;
            const TInt rd = (instruction >> 12) & 15;

            if (rd != 15)
                cpu.r[rd] = useSpsr ? GetCurrentSpsr(cpu) : cpu.cpsr;

            return ENgbaCpuOk;
        }

        // MSR (register): tulis register ke CPSR atau SPSR.
        if ((instruction & 0x0FB0FFF0u) == 0x0120F000u)
        {
            const TUint32 operand =
                ReadRegister(cpu, instruction & 15, address);
            return ExecuteMsr(cpu, instruction, operand);
        }

        // MSR (immediate): tulis immediate ke CPSR atau SPSR.
        if ((instruction & 0x0FB0F000u) == 0x0320F000u)
        {
            const TInt rotate =
                static_cast<TInt>((instruction >> 8) & 15) * 2;
            const TUint32 operand =
                RotateRight(instruction & 0xFFu, rotate);
            return ExecuteMsr(cpu, instruction, operand);
        }

        // BIOS SWI - ARM mode.
        if ((instruction & 0x0F000000u) == 0x0F000000u)
        {
            const TUint32 swiNumber =
                (instruction >> 16) & 0xFFu;

            return ExecuteBiosSwi(cpu, swiNumber, address);
        }

        // Multiply / Multiply Accumulate (32-bit).
        if ((instruction & 0x0FC000F0u) == 0x00000090u)
            return ExecuteMultiply(cpu, instruction);

        // Multiply Long (UMULL / UMLAL / SMULL / SMLAL 64-bit).
        if ((instruction & 0x0F8000F0u) == 0x00800090u)
            return ExecuteMultiplyLong(cpu, instruction);

        // Single Data Swap (SWP / SWPB).
        if ((instruction & 0x0FB00FF0u) == 0x01000090u)
            return ExecuteSwap(cpu, instruction);

        // Halfword and signed-byte transfers.
        if ((instruction & 0x0E000090u) == 0x00000090u)
            return ExecuteHalfwordTransfer(cpu, instruction, address);

        // Block data transfer (LDM / STM).
        if ((instruction & 0x0E000000u) == 0x08000000u)
            return ExecuteBlockTransfer(cpu, instruction, address);

        // B / BL.
        if ((instruction & 0x0E000000u) == 0x0A000000u)
        {
            TInt32 displacement =
                static_cast<TInt32>(instruction & 0x00FFFFFFu);

            if (displacement & 0x00800000)
                displacement |= static_cast<TInt32>(0xFF000000u);

            displacement *= 4;

            if (instruction & (1u << 24))
                cpu.r[14] = address + 4u;

            SetThumbState(cpu, EFalse);

            cpu.r[15] = address + 8u +
                        static_cast<TUint32>(displacement);

            return ENgbaCpuOk;
        }

        // LDR / STR / LDRB / STRB.
        if ((instruction & 0x0C000000u) == 0x04000000u)
        {
            const TBool pre = (instruction & (1u << 24)) != 0;
            const TBool add = (instruction & (1u << 23)) != 0;
            const TBool byte = (instruction & (1u << 22)) != 0;
            const TBool writeBack = (instruction & (1u << 21)) != 0;
            const TBool load = (instruction & (1u << 20)) != 0;

            const TInt rn = (instruction >> 16) & 15;
            const TInt rd = (instruction >> 12) & 15;

            if ((instruction & (1u << 25)) &&
                (instruction & (1u << 4)))
                return ENgbaCpuUnsupported;

            const TUint32 base = ReadRegister(cpu, rn, address);
            TUint32 offset = instruction & 0xFFFu;

            if (instruction & (1u << 25))
            {
                TBool carry = (cpu.cpsr & KFlagC) != 0;

                const TInt shiftType =
                    (instruction >> 5) & 3;

                const TInt shiftAmount =
                    (instruction >> 7) & 31;

                const TUint32 rm =
                    ReadRegister(cpu, instruction & 15, address);

                if (shiftAmount == 0)
                {
                    if (shiftType == 0)
                    {
                        offset = rm;
                    }
                    else if (shiftType == 1)
                    {
                        offset = 0;
                        carry = (rm & 0x80000000u) != 0;
                    }
                    else if (shiftType == 2)
                    {
                        carry = (rm & 0x80000000u) != 0;
                        offset = carry ? 0xFFFFFFFFu : 0;
                    }
                    else
                    {
                        carry = (rm & 1u) != 0;

                        offset = (rm >> 1) |
                            ((cpu.cpsr & KFlagC)
                                ? 0x80000000u : 0);
                    }
                }
                else
                {
                    offset = ShiftValue(
                        rm,
                        shiftType,
                        shiftAmount,
                        carry
                    );
                }
            }

            const TUint32 adjusted = add ? base + offset : base - offset;
            const TUint32 memoryAddress = pre ? adjusted : base;

            if (load)
            {
                const TUint32 value = byte
                    ? NgbaMemoryRead8(memoryAddress)
                    : ReadWordRotated(memoryAddress);

                if (!pre || writeBack)
                {
                    if (rn != 15)
                        cpu.r[rn] = adjusted;
                }

                if (rd == 15)
                {
                    SetThumbState(cpu, EFalse);
                    cpu.r[15] = value & ~3u;
                }
                else
                {
                    cpu.r[rd] = value;
                }
            }
            else
            {
                const TUint32 value = ReadRegister(cpu, rd, address);

                if (byte)
                    NgbaMemoryWrite8(
                        memoryAddress,
                        static_cast<TUint8>(value)
                    );
                else
                    NgbaMemoryWrite32(memoryAddress & ~3u, value);

                if (!pre || writeBack)
                {
                    if (rn != 15)
                        cpu.r[rn] = adjusted;
                }
            }

            return ENgbaCpuOk;
        }

        // Data processing.
        if ((instruction & 0x0C000000u) == 0)
        {
            const TInt opcode = (instruction >> 21) & 15;
            const TBool setFlags = (instruction & (1u << 20)) != 0;

            const TInt rn = (instruction >> 16) & 15;
            const TInt rd = (instruction >> 12) & 15;

            const TUint32 left = ReadRegister(cpu, rn, address);

            TBool carry = EFalse;

            const TUint32 right = DecodeOperand2(
                cpu,
                instruction,
                address,
                carry
            );

            TUint32 result = 0;
            TBool writeResult = ETrue;
            TBool arithmetic = EFalse;
            TBool subtract = EFalse;
            TUint32 carryIn = 0;

            switch (opcode)
            {
            case 0:
                result = left & right;
                break;

            case 1:
                result = left ^ right;
                break;

            case 2:
                result = left - right;
                arithmetic = ETrue;
                subtract = ETrue;
                break;

            case 3:
                result = right - left;
                arithmetic = ETrue;
                subtract = ETrue;
                break;

            case 4:
                result = left + right;
                arithmetic = ETrue;
                break;

            case 5:
                carryIn = (cpu.cpsr & KFlagC) ? 1u : 0u;
                result = left + right + carryIn;
                arithmetic = ETrue;
                break;

            case 6:
                carryIn = (cpu.cpsr & KFlagC) ? 0u : 1u;
                result = left - right - carryIn;
                arithmetic = ETrue;
                subtract = ETrue;
                break;

            case 7:
                carryIn = (cpu.cpsr & KFlagC) ? 0u : 1u;
                result = right - left - carryIn;
                arithmetic = ETrue;
                subtract = ETrue;
                break;

            case 8:
                result = left & right;
                writeResult = EFalse;
                break;

            case 9:
                result = left ^ right;
                writeResult = EFalse;
                break;

            case 10:
                result = left - right;
                arithmetic = ETrue;
                subtract = ETrue;
                writeResult = EFalse;
                break;

            case 11:
                result = left + right;
                arithmetic = ETrue;
                writeResult = EFalse;
                break;

            case 12:
                result = left | right;
                break;

            case 13:
                result = right;
                break;

            case 14:
                result = left & ~right;
                break;

            case 15:
                result = ~right;
                break;

            default:
                return ENgbaCpuUnsupported;
            }

            if (writeResult && rd == 15)
            {
                if (setFlags)
                {
                    const TUint32 spsr = GetCurrentSpsr(cpu);
                    SetCpsrWithMode(cpu, spsr);
                    cpu.r[15] = cpu.thumb
                        ? (result & ~1u)
                        : (result & ~3u);
                }
                else
                {
                    SetThumbState(cpu, EFalse);
                    cpu.r[15] = result & ~3u;
                }

                return ENgbaCpuOk;
            }

            if (writeResult)
                cpu.r[rd] = result;

            if (setFlags || !writeResult)
            {
                if (arithmetic)
                {
                    SetArithmeticFlags(
                        cpu,
                        (opcode == 3 || opcode == 7) ? right : left,
                        (opcode == 3 || opcode == 7) ? left : right,
                        result,
                        subtract,
                        carryIn
                    );
                }
                else
                {
                    SetNzFlags(cpu, result);
                    SetCarry(cpu, carry);
                }
            }

            return ENgbaCpuOk;
        }

        return ENgbaCpuUnsupported;
    }

    // ---------------------------------------------------------
    // THUMB INSTRUCTIONS
    // ---------------------------------------------------------

    TInt ExecuteThumb(
        TNgbaCpuState& cpu,
        TUint16 instruction,
        TUint32 address
    )
    {
        // LSL / LSR / ASR immediate.
        if ((instruction & 0xE000u) == 0x0000u &&
            (instruction & 0x1800u) != 0x1800u)
        {
            const TInt type = (instruction >> 11) & 3;

            if (type == 3)
                return ENgbaCpuUnsupported;

            const TInt amount = (instruction >> 6) & 31;
            const TInt rs = (instruction >> 3) & 7;
            const TInt rd = instruction & 7;

            TBool carry = (cpu.cpsr & KFlagC) != 0;

            const TUint32 result = ShiftValue(
                cpu.r[rs],
                type,
                amount == 0 && type != 0 ? 32 : amount,
                carry
            );

            cpu.r[rd] = result;
            SetNzFlags(cpu, result);
            SetCarry(cpu, carry);

            return ENgbaCpuOk;
        }

        // ADD / SUB register or immediate.
        if ((instruction & 0xF800u) == 0x1800u)
        {
            const TBool immediate = (instruction & (1u << 10)) != 0;
            const TBool subtract = (instruction & (1u << 9)) != 0;

            const TInt rd = instruction & 7;
            const TInt rn = (instruction >> 3) & 7;

            const TUint32 left = cpu.r[rn];
            const TUint32 right = immediate
                ? ((instruction >> 6) & 7)
                : cpu.r[(instruction >> 6) & 7];

            const TUint32 result =
                subtract ? left - right : left + right;

            cpu.r[rd] = result;
            SetArithmeticFlags(cpu, left, right, result, subtract);

            return ENgbaCpuOk;
        }

        // MOV / CMP / ADD / SUB immediate.
        if ((instruction & 0xE000u) == 0x2000u)
        {
            const TInt opcode = (instruction >> 11) & 3;
            const TInt rd = (instruction >> 8) & 7;
            const TUint32 value = instruction & 0xFFu;
            const TUint32 old = cpu.r[rd];

            TUint32 result = value;

            switch (opcode)
            {
            case 0:
                cpu.r[rd] = result;
                SetNzFlags(cpu, result);
                break;

            case 1:
                result = old - value;
                SetArithmeticFlags(cpu, old, value, result, ETrue);
                break;

            case 2:
                result = old + value;
                cpu.r[rd] = result;
                SetArithmeticFlags(cpu, old, value, result, EFalse);
                break;

            case 3:
                result = old - value;
                cpu.r[rd] = result;
                SetArithmeticFlags(cpu, old, value, result, ETrue);
                break;
            }

            return ENgbaCpuOk;
        }

        // ALU operations.
        if ((instruction & 0xFC00u) == 0x4000u)
        {
            const TInt opcode = (instruction >> 6) & 15;
            const TInt rs = (instruction >> 3) & 7;
            const TInt rd = instruction & 7;

            const TUint32 left = cpu.r[rd];
            const TUint32 right = cpu.r[rs];

            TUint32 result = left;
            TBool carry = (cpu.cpsr & KFlagC) != 0;

            switch (opcode)
            {
            case 0:
                result = left & right;
                break;

            case 1:
                result = left ^ right;
                break;

            case 2:
                result = ShiftValue(left, 0, right & 0xFFu, carry);
                break;

            case 3:
                result = ShiftValue(left, 1, right & 0xFFu, carry);
                break;

            case 4:
                result = ShiftValue(left, 2, right & 0xFFu, carry);
                break;

            case 5:
            {
                const TUint32 oldCarry =
                    (cpu.cpsr & KFlagC) ? 1u : 0u;

                result = left + right + oldCarry;

                SetArithmeticFlags(
                    cpu, left, right, result, EFalse, oldCarry
                );

                cpu.r[rd] = result;
                return ENgbaCpuOk;
            }

            case 6:
            {
                const TUint32 borrow =
                    (cpu.cpsr & KFlagC) ? 0u : 1u;

                result = left - right - borrow;

                SetArithmeticFlags(
                    cpu, left, right, result, ETrue, borrow
                );

                cpu.r[rd] = result;
                return ENgbaCpuOk;
            }

            case 7:
                result = ShiftValue(left, 3, right & 0xFFu, carry);
                break;

            case 8:
                result = left & right;
                SetNzFlags(cpu, result);
                return ENgbaCpuOk;

            case 9:
                result = 0u - right;
                SetArithmeticFlags(cpu, 0, right, result, ETrue);
                cpu.r[rd] = result;
                return ENgbaCpuOk;

            case 10:
                result = left - right;
                SetArithmeticFlags(cpu, left, right, result, ETrue);
                return ENgbaCpuOk;

            case 11:
                result = left + right;
                SetArithmeticFlags(cpu, left, right, result, EFalse);
                return ENgbaCpuOk;

            case 12:
                result = left | right;
                break;

            case 13:
                result = left * right;
                break;

            case 14:
                result = left & ~right;
                break;

            case 15:
                result = ~right;
                break;
            }

            cpu.r[rd] = result;
            SetNzFlags(cpu, result);
            SetCarry(cpu, carry);

            return ENgbaCpuOk;
        }

        // Register-offset load/store.
        if ((instruction & 0xF000u) == 0x5000u)
        {
            const TInt operation = (instruction >> 9) & 7;
            const TInt ro = (instruction >> 6) & 7;
            const TInt rb = (instruction >> 3) & 7;
            const TInt rd = instruction & 7;

            const TUint32 memoryAddress = cpu.r[rb] + cpu.r[ro];

            switch (operation)
            {
            case 0:
                NgbaMemoryWrite32(memoryAddress & ~3u, cpu.r[rd]);
                break;

            case 1:
                NgbaMemoryWrite16(
                    memoryAddress & ~1u,
                    static_cast<TUint16>(cpu.r[rd])
                );
                break;

            case 2:
                NgbaMemoryWrite8(
                    memoryAddress,
                    static_cast<TUint8>(cpu.r[rd])
                );
                break;

            case 3:
                cpu.r[rd] = static_cast<TUint32>(
                    static_cast<TInt32>(
                        static_cast<TInt8>(
                            NgbaMemoryRead8(memoryAddress)
                        )
                    )
                );
                break;

            case 4:
                cpu.r[rd] = ReadWordRotated(memoryAddress);
                break;

            case 5:
                cpu.r[rd] = ReadHalfwordGba(memoryAddress);
                break;

            case 6:
                cpu.r[rd] = NgbaMemoryRead8(memoryAddress);
                break;

            case 7:
                if (memoryAddress & 1u)
                {
                    cpu.r[rd] = static_cast<TUint32>(
                        static_cast<TInt32>(
                            static_cast<TInt8>(
                                NgbaMemoryRead8(memoryAddress)
                            )
                        )
                    );
                }
                else
                {
                    cpu.r[rd] = static_cast<TUint32>(
                        static_cast<TInt32>(
                            static_cast<TInt16>(
                                NgbaMemoryRead16(memoryAddress)
                            )
                        )
                    );
                }
                break;
            }

            return ENgbaCpuOk;
        }

        // LDR literal.
        if ((instruction & 0xF800u) == 0x4800u)
        {
            const TInt rd = (instruction >> 8) & 7;
            const TUint32 offset = (instruction & 0xFFu) * 4u;
            const TUint32 base = (address + 4u) & ~3u;
            const TUint32 memoryAddress = base + offset;

            cpu.r[rd] = NgbaMemoryRead32(memoryAddress);

            return ENgbaCpuOk;
        }

        // STR / LDR / STRB / LDRB immediate.
        if ((instruction & 0xE000u) == 0x6000u)
        {
            const TBool byte = (instruction & 0x1000u) != 0;
            const TBool load = (instruction & 0x0800u) != 0;

            const TInt offset = (instruction >> 6) & 31;
            const TInt rb = (instruction >> 3) & 7;
            const TInt rd = instruction & 7;

            const TUint32 memoryAddress =
                cpu.r[rb] + (byte ? offset : offset * 4);

            if (load)
            {
                cpu.r[rd] = byte
                    ? NgbaMemoryRead8(memoryAddress)
                    : ReadWordRotated(memoryAddress);
            }
            else
            {
                if (byte)
                    NgbaMemoryWrite8(
                        memoryAddress,
                        static_cast<TUint8>(cpu.r[rd])
                    );
                else
                    NgbaMemoryWrite32(memoryAddress & ~3u, cpu.r[rd]);
            }

            return ENgbaCpuOk;
        }

        // STRH / LDRH immediate.
        if ((instruction & 0xF000u) == 0x8000u)
        {
            const TBool load = (instruction & 0x0800u) != 0;
            const TInt offset = (instruction >> 6) & 31;
            const TInt rb = (instruction >> 3) & 7;
            const TInt rd = instruction & 7;

            const TUint32 memoryAddress = cpu.r[rb] + offset * 2;

            if (load)
                cpu.r[rd] = ReadHalfwordGba(memoryAddress);
            else
                NgbaMemoryWrite16(
                    memoryAddress & ~1u,
                    static_cast<TUint16>(cpu.r[rd])
                );

            return ENgbaCpuOk;
        }

        // SP-relative load/store.
        if ((instruction & 0xF000u) == 0x9000u)
        {
            const TBool load = (instruction & 0x0800u) != 0;
            const TInt rd = (instruction >> 8) & 7;
            const TUint32 offset = (instruction & 0xFFu) * 4;
            const TUint32 memoryAddress = cpu.r[13] + offset;

            if (load)
                cpu.r[rd] = ReadWordRotated(memoryAddress);
            else
                NgbaMemoryWrite32(memoryAddress & ~3u, cpu.r[rd]);

            return ENgbaCpuOk;
        }

        // Load address.
        if ((instruction & 0xF000u) == 0xA000u)
        {
            const TInt rd = (instruction >> 8) & 7;
            const TUint32 offset = (instruction & 0xFFu) * 4;

            if (instruction & 0x0800u)
                cpu.r[rd] = cpu.r[13] + offset;
            else
                cpu.r[rd] = ((address + 4u) & ~3u) + offset;

            return ENgbaCpuOk;
        }

        // ADD / SUB SP.
        if ((instruction & 0xFF00u) == 0xB000u)
        {
            const TUint32 offset = (instruction & 0x7Fu) * 4;

            if (instruction & 0x0080u)
                cpu.r[13] -= offset;
            else
                cpu.r[13] += offset;

            return ENgbaCpuOk;
        }

        // PUSH / POP.
        if ((instruction & 0xF600u) == 0xB400u)
        {
            const TBool pop = (instruction & 0x0800u) != 0;
            const TBool extra = (instruction & 0x0100u) != 0;
            const TUint32 list = instruction & 0x00FFu;

            TInt count = 0;

            for (TInt i = 0; i < 8; ++i)
                if (list & (1u << i))
                    ++count;

            if (extra)
                ++count;

            if (count == 0)
                return ENgbaCpuUnsupported;

            if (!pop)
            {
                TUint32 memoryAddress = cpu.r[13] - count * 4u;

                for (TInt i = 0; i < 8; ++i)
                {
                    if (list & (1u << i))
                    {
                        NgbaMemoryWrite32(memoryAddress, cpu.r[i]);
                        memoryAddress += 4u;
                    }
                }

                if (extra)
                    NgbaMemoryWrite32(memoryAddress, cpu.r[14]);

                cpu.r[13] -= count * 4u;
            }
            else
            {
                TUint32 memoryAddress = cpu.r[13];

                for (TInt i = 0; i < 8; ++i)
                {
                    if (list & (1u << i))
                    {
                        cpu.r[i] = NgbaMemoryRead32(memoryAddress);
                        memoryAddress += 4u;
                    }
                }

                if (extra)
                {
                    const TUint32 value =
                        NgbaMemoryRead32(memoryAddress);

                    SetThumbState(cpu, ETrue);
                    cpu.r[15] = value & ~1u;

                    memoryAddress += 4u;
                }

                cpu.r[13] = memoryAddress;
            }

            return ENgbaCpuOk;
        }

        // Multiple load/store.
        if ((instruction & 0xF000u) == 0xC000u)
        {
            const TBool load = (instruction & 0x0800u) != 0;
            const TInt rb = (instruction >> 8) & 7;
            const TUint32 list = instruction & 0xFFu;

            if (list == 0)
                return ENgbaCpuUnsupported;

            TUint32 memoryAddress = cpu.r[rb];

            for (TInt i = 0; i < 8; ++i)
            {
                if (!(list & (1u << i)))
                    continue;

                if (load)
                    cpu.r[i] = NgbaMemoryRead32(memoryAddress);
                else
                    NgbaMemoryWrite32(memoryAddress, cpu.r[i]);

                memoryAddress += 4u;
            }

            if (!(load && (list & (1u << rb))))
                cpu.r[rb] = memoryAddress;

            return ENgbaCpuOk;
        }

        // BIOS SWI - Thumb mode.
        if ((instruction & 0xFF00u) == 0xDF00u)
        {
            const TUint32 swiNumber = instruction & 0xFFu;
            return ExecuteBiosSwi(cpu, swiNumber, address);
        }

        // Conditional branch.
        if ((instruction & 0xF000u) == 0xD000u)
        {
            const TInt condition = (instruction >> 8) & 15;

            if (condition >= 14)
                return ENgbaCpuUnsupported;

            if (ConditionPassed(cpu.cpsr, condition))
            {
                TInt32 offset =
                    static_cast<TInt8>(instruction & 0xFFu);

                offset *= 2;

                SetThumbState(cpu, ETrue);

                cpu.r[15] = address + 4u +
                            static_cast<TUint32>(offset);
            }

            return ENgbaCpuOk;
        }

        // Unconditional branch.
        if ((instruction & 0xF800u) == 0xE000u)
        {
            TInt32 offset =
                static_cast<TInt16>(instruction & 0x07FFu);

            if (offset & 0x0400)
                offset |= static_cast<TInt16>(0xF800);

            offset *= 2;

            SetThumbState(cpu, ETrue);

            cpu.r[15] = address + 4u +
                        static_cast<TUint32>(offset);

            return ENgbaCpuOk;
        }

        // Long branch with link: first half.
        if ((instruction & 0xF800u) == 0xF000u)
        {
            TInt32 offset =
                static_cast<TInt32>(instruction & 0x07FFu);

            if (offset & 0x0400)
                offset |= static_cast<TInt32>(0xFFFFF800u);

            cpu.r[14] = address + 4u +
                        static_cast<TUint32>(offset * 4096);

            return ENgbaCpuOk;
        }

        // Long branch with link: second half.
        if ((instruction & 0xF800u) == 0xF800u)
        {
            const TUint32 target =
                cpu.r[14] + ((instruction & 0x07FFu) * 2u);

            cpu.r[14] = (address + 2u) | 1u;

            SetThumbState(cpu, ETrue);
            cpu.r[15] = target & ~1u;

            return ENgbaCpuOk;
        }

        // BX: perpindahan ARM / Thumb.
        if ((instruction & 0xFF87u) == 0x4700u)
        {
            const TInt rm = (instruction >> 3) & 15;
            const TUint32 target = ReadRegister(cpu, rm, address);

            const TBool thumb = (target & 1u) != 0;

            SetThumbState(cpu, thumb);

            cpu.r[15] = thumb
                ? (target & ~1u)
                : (target & ~3u);

            return ENgbaCpuOk;
        }

        // High-register ADD (0) / CMP (1) / MOV (2).
        if ((instruction & 0xFC00u) == 0x4400u)
        {
            const TInt opcode = (instruction >> 8) & 3;

            const TInt rd =
                (instruction & 7) |
                ((instruction >> 4) & 8);

            const TInt rs = (instruction >> 3) & 15;
            const TUint32 value = ReadRegister(cpu, rs, address);

            if (opcode == 0)
            {
                if (rd == 15)
                {
                    const TUint32 pcValue = address + 4u;
                    SetThumbState(cpu, ETrue);
                    cpu.r[15] = (pcValue + value) & ~1u;
                }
                else
                {
                    cpu.r[rd] += value;
                }
            }
            else if (opcode == 1)
            {
                const TUint32 left = ReadRegister(cpu, rd, address);
                const TUint32 result = left - value;

                SetArithmeticFlags(
                    cpu, left, value, result, ETrue
                );
            }
            else if (opcode == 2)
            {
                if (rd == 15)
                {
                    SetThumbState(cpu, ETrue);
                    cpu.r[15] = value & ~1u;
                }
                else
                {
                    cpu.r[rd] = value;
                }
            }
            else
            {
                return ENgbaCpuUnsupported;
            }

            return ENgbaCpuOk;
        }

        return ENgbaCpuUnsupported;
    }

    // ---------------------------------------------------------
    // HARDWARE & HLE BIOS IRQ DISPATCH
    // ---------------------------------------------------------

    TBool DispatchPendingIrq(TNgbaCpuState& cpu, TUint32 address)
    {
        if ((cpu.cpsr & KFlagI) != 0)
            return EFalse;

        if ((NgbaMemoryRead16(0x04000208u) & 1u) == 0)
            return EFalse;

        const TUint16 ie = NgbaMemoryRead16(0x04000200u);
        const TUint16 irf = NgbaMemoryRead16(0x04000202u);

        if ((ie & irf) == 0)
            return EFalse;

        // Jika BIOS asli terpetakan di 0x00000018, masuk ke vektor hardware IRQ.
        if (NgbaMemoryRead32(0x00000018u) != 0)
        {
            const TUint32 oldCpsr = cpu.cpsr;
            SwitchCpuMode(cpu, KModeIrq);
            gBank.spsr_irq = oldCpsr;
            cpu.cpsr |= KFlagI;
            SetThumbState(cpu, EFalse);
            cpu.r[14] = address + 4u;
            cpu.r[15] = 0x00000018u;
            ++cpu.cycles;
            return ETrue;
        }

        // HLE BIOS IRQ handler melalui pointer [0x03007FFC].
        const TUint32 isrHandler = NgbaMemoryRead32(0x03007FFCu);

        if (isrHandler == 0)
            return EFalse;

        const TUint32 oldCpsr = cpu.cpsr;
        SwitchCpuMode(cpu, KModeIrq);
        gBank.spsr_irq = oldCpsr;
        cpu.cpsr |= KFlagI;
        SetThumbState(cpu, EFalse);
        cpu.r[14] = address + 4u;

        // Push {r0-r3, r12, lr} ke SP_irq.
        cpu.r[13] -= 24u;
        NgbaMemoryWrite32(cpu.r[13] + 0u, cpu.r[0]);
        NgbaMemoryWrite32(cpu.r[13] + 4u, cpu.r[1]);
        NgbaMemoryWrite32(cpu.r[13] + 8u, cpu.r[2]);
        NgbaMemoryWrite32(cpu.r[13] + 12u, cpu.r[3]);
        NgbaMemoryWrite32(cpu.r[13] + 16u, cpu.r[12]);
        NgbaMemoryWrite32(cpu.r[13] + 20u, cpu.r[14]);

        cpu.r[0] = 0x04000000u;
        cpu.r[14] = KHleIrqReturnAddress;
        gBank.irqTrampolineActive = ETrue;

        if (isrHandler & 1u)
        {
            SetThumbState(cpu, ETrue);
            cpu.r[15] = isrHandler & ~1u;
        }
        else
        {
            SetThumbState(cpu, EFalse);
            cpu.r[15] = isrHandler & ~3u;
        }

        ++cpu.cycles;
        return ETrue;
    }
}

// ---------------------------------------------------------
// CPU RESET
// ---------------------------------------------------------

void NgbaCpuReset(TNgbaCpuState& cpu)
{
    for (TInt i = 0; i < 16; ++i)
        cpu.r[i] = 0;

    for (TInt i = 0; i < 5; ++i)
    {
        gBank.r8_usr[i] = 0;
        gBank.r8_fiq[i] = 0;
    }

    gBank.r13_usr = 0x03007F00u;
    gBank.r14_usr = 0;
    gBank.r13_fiq = 0;
    gBank.r14_fiq = 0;
    gBank.r13_irq = 0x03007FA0u;
    gBank.r14_irq = 0;
    gBank.r13_svc = 0x03007FE0u;
    gBank.r14_svc = 0;
    gBank.r13_abt = 0;
    gBank.r14_abt = 0;
    gBank.r13_und = 0;
    gBank.r14_und = 0;
    gBank.spsr_fiq = 0x00000010u;
    gBank.spsr_irq = 0x00000010u;
    gBank.spsr_svc = 0x00000010u;
    gBank.spsr_abt = 0x00000010u;
    gBank.spsr_und = 0x00000010u;
    gBank.intrWaitMask = 0;
    gBank.irqTrampolineActive = EFalse;

    cpu.r[15] = KRomBase;
    cpu.r[13] = 0x03007F00u;

    cpu.cpsr = 0x000000DFu;

    cpu.halted = EFalse;
    SetThumbState(cpu, EFalse);

    cpu.cycles = 0;

    cpu.lastAddress = KRomBase;
    cpu.lastOpcode = 0;
    cpu.lastInstructionSize = 0;
}

// ---------------------------------------------------------
// CPU STEP
// ---------------------------------------------------------

TInt NgbaCpuStep(TNgbaCpuState& cpu)
{
    if (!NgbaMemoryReady())
        return ENgbaCpuBadArgument;

    // Sinkronkan status T pada CPSR dengan mode CPU.
    SetThumbState(cpu, cpu.thumb);

    if (cpu.halted)
    {
        const TUint16 pending =
            NgbaMemoryRead16(0x04000200u) &
            NgbaMemoryRead16(0x04000202u);

        if (pending == 0)
        {
            ++cpu.cycles;
            return ENgbaCpuOk;
        }

        cpu.halted = EFalse;
    }

    const TUint32 address = cpu.r[15];

    // Kembalian HLE BIOS IRQ: pop {r0-r3, r12, lr} dan subs pc, lr, #4.
    if (!cpu.thumb &&
        address == KHleIrqReturnAddress &&
        gBank.irqTrampolineActive)
    {
        cpu.r[0] = NgbaMemoryRead32(cpu.r[13] + 0u);
        cpu.r[1] = NgbaMemoryRead32(cpu.r[13] + 4u);
        cpu.r[2] = NgbaMemoryRead32(cpu.r[13] + 8u);
        cpu.r[3] = NgbaMemoryRead32(cpu.r[13] + 12u);
        cpu.r[12] = NgbaMemoryRead32(cpu.r[13] + 16u);
        cpu.r[14] = NgbaMemoryRead32(cpu.r[13] + 20u);
        cpu.r[13] += 24u;

        gBank.irqTrampolineActive = EFalse;

        const TUint32 returnAddress = cpu.r[14] - 4u;
        const TUint32 restoredCpsr = GetCurrentSpsr(cpu);
        SetCpsrWithMode(cpu, restoredCpsr);

        cpu.r[15] = cpu.thumb
            ? (returnAddress & ~1u)
            : (returnAddress & ~3u);

        ++cpu.cycles;
        return ENgbaCpuOk;
    }

    // Jika emulator luar melompat ke vektor 0x00000018 tanpa file BIOS.
    if (!cpu.thumb &&
        address == 0x00000018u &&
        NgbaMemoryRead32(0x00000018u) == 0)
    {
        const TUint32 isrHandler = NgbaMemoryRead32(0x03007FFCu);

        if (isrHandler != 0)
        {
            cpu.r[13] -= 24u;
            NgbaMemoryWrite32(cpu.r[13] + 0u, cpu.r[0]);
            NgbaMemoryWrite32(cpu.r[13] + 4u, cpu.r[1]);
            NgbaMemoryWrite32(cpu.r[13] + 8u, cpu.r[2]);
            NgbaMemoryWrite32(cpu.r[13] + 12u, cpu.r[3]);
            NgbaMemoryWrite32(cpu.r[13] + 16u, cpu.r[12]);
            NgbaMemoryWrite32(cpu.r[13] + 20u, cpu.r[14]);

            cpu.r[0] = 0x04000000u;
            cpu.r[14] = KHleIrqReturnAddress;
            gBank.irqTrampolineActive = ETrue;

            if (isrHandler & 1u)
            {
                SetThumbState(cpu, ETrue);
                cpu.r[15] = isrHandler & ~1u;
            }
            else
            {
                SetThumbState(cpu, EFalse);
                cpu.r[15] = isrHandler & ~3u;
            }

            ++cpu.cycles;
            return ENgbaCpuOk;
        }
    }

    if (DispatchPendingIrq(cpu, address))
        return ENgbaCpuOk;

    const TBool previousThumb = cpu.thumb;

    cpu.lastAddress = address;
    cpu.lastOpcode = 0;
    cpu.lastInstructionSize = 0;

    // THUMB MODE: instruksi 16-bit.
    if (previousThumb)
    {
        if (address & 1u)
            return ENgbaCpuBadArgument;

        const TUint16 instruction =
            NgbaMemoryRead16(address);

        cpu.lastOpcode = instruction;
        cpu.lastInstructionSize = 2;

        cpu.r[15] = address + 2u;

        const TInt result =
            ExecuteThumb(cpu, instruction, address);

        if (result == ENgbaCpuOk)
        {
            SetThumbState(cpu, cpu.thumb);
            ++cpu.cycles;
        }
        else
        {
            cpu.r[15] = address;
            SetThumbState(cpu, previousThumb);
        }

        return result;
    }

    // ARM MODE: instruksi 32-bit.
    if (address & 3u)
        return ENgbaCpuBadArgument;

    const TUint32 instruction =
        NgbaMemoryRead32(address);

    cpu.lastOpcode = instruction;
    cpu.lastInstructionSize = 4;

    cpu.r[15] = address + 4u;

    const TInt result =
        ExecuteArm(cpu, instruction, address);

    if (result == ENgbaCpuOk)
    {
        SetThumbState(cpu, cpu.thumb);
        ++cpu.cycles;
    }
    else
    {
        cpu.r[15] = address;
        SetThumbState(cpu, previousThumb);
    }

    return result;
}

// ---------------------------------------------------------
// REGISTER ACCESS
// ---------------------------------------------------------

TUint32 NgbaCpuGetRegister(
    const TNgbaCpuState& cpu,
    TInt index
)
{
    if (index < 0 || index > 15)
        return 0;

    return cpu.r[index];
}

TUint32 NgbaCpuGetCpsr(const TNgbaCpuState& cpu)
{
    return cpu.cpsr;
}

TBool NgbaCpuIsThumb(const TNgbaCpuState& cpu)
{
    return cpu.thumb;
}

TUint32 NgbaCpuGetLastAddress(const TNgbaCpuState& cpu)
{
    return cpu.lastAddress;
}

TUint32 NgbaCpuGetLastOpcode(const TNgbaCpuState& cpu)
{
    return cpu.lastOpcode;
}

TUint8 NgbaCpuGetLastInstructionSize(const TNgbaCpuState& cpu)
{
    return cpu.lastInstructionSize;
}

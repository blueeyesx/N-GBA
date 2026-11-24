
#include "arm7tdmi.h"
#include "gba_memory.h"

namespace
{
    const TUint32 KFlagN = 0x80000000u;
    const TUint32 KFlagZ = 0x40000000u;
    const TUint32 KFlagC = 0x20000000u;
    const TUint32 KFlagV = 0x10000000u;

    const TUint32 KRomBase = 0x08000000u;

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
            return address + (cpu.thumb ? 4u : 8u);

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

                return carry
                    ? 0xFFFFFFFFu
                    : 0;
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
        TBool subtract
    )
    {
        SetNzFlags(cpu, result);
        cpu.cpsr &= ~(KFlagC | KFlagV);

        if (subtract)
        {
            if (left >= right)
                cpu.cpsr |= KFlagC;

            const TUint32 overflow =
                (left ^ right) & (left ^ result);

            if (overflow & 0x80000000u)
                cpu.cpsr |= KFlagV;
        }
        else
        {
            const TUint64 wide =
                static_cast<TUint64>(left) +
                static_cast<TUint64>(right);

            if (wide >> 32)
                cpu.cpsr |= KFlagC;

            const TUint32 overflow =
                ~(left ^ right) & (left ^ result);

            if (overflow & 0x80000000u)
                cpu.cpsr |= KFlagV;
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
            const TInt amount =
                static_cast<TInt>(
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

        TUint32 result =
            cpu.r[rm] * cpu.r[rs];

        if (accumulate)
            result += cpu.r[rn];

        cpu.r[rd] = result;

        if (setFlags)
            SetNzFlags(cpu, result);

        return ENgbaCpuOk;
    }

    TInt ExecuteHalfwordTransfer(
        TNgbaCpuState& cpu,
        TUint32 instruction,
        TUint32 address
    )
    {
        const TBool pre =
            (instruction & (1u << 24)) != 0;

        const TBool add =
            (instruction & (1u << 23)) != 0;

        const TBool immediate =
            (instruction & (1u << 22)) != 0;

        const TBool writeBack =
            (instruction & (1u << 21)) != 0;

        const TBool load =
            (instruction & (1u << 20)) != 0;

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

        const TUint32 base =
            ReadRegister(cpu, rn, address);

        const TUint32 adjusted =
            add ? base + offset : base - offset;

        const TUint32 memoryAddress =
            pre ? adjusted : base;

        if (load)
        {
            TUint32 value = 0;

            if (operation == 1)
            {
                value = NgbaMemoryRead16(memoryAddress);
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
                value = static_cast<TUint32>(
                    static_cast<TInt32>(
                        static_cast<TInt16>(
                            NgbaMemoryRead16(memoryAddress)
                        )
                    )
                );
            }

            if (rd == 15)
                cpu.r[15] = value & ~3u;
            else
                cpu.r[rd] = value;
        }
        else
        {
            NgbaMemoryWrite16(
                memoryAddress,
                static_cast<TUint16>(
                    ReadRegister(cpu, rd, address)
                )
            );
        }

        if (!pre || writeBack)
        {
            if (rn != 15)
                cpu.r[rn] = adjusted;
        }

        return ENgbaCpuOk;
    }

    TInt ExecuteBlockTransfer(
        TNgbaCpuState& cpu,
        TUint32 instruction,
        TUint32 address
    )
    {
        const TBool pre =
            (instruction & (1u << 24)) != 0;

        const TBool add =
            (instruction & (1u << 23)) != 0;

        const TBool userMode =
            (instruction & (1u << 22)) != 0;

        const TBool writeBack =
            (instruction & (1u << 21)) != 0;

        const TBool load =
            (instruction & (1u << 20)) != 0;

        const TInt rn = (instruction >> 16) & 15;
        const TUint32 list = instruction & 0xFFFFu;

        if (list == 0 || userMode)
            return ENgbaCpuUnsupported;

        const TUint32 base =
            ReadRegister(cpu, rn, address);

        TInt count = 0;

        for (TInt i = 0; i < 16; ++i)
        {
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
            add
                ? base + count * 4u
                : base - count * 4u;

        for (TInt i = 0; i < 16; ++i)
        {
            if (!(list & (1u << i)))
                continue;

            if (load)
            {
                const TUint32 value =
                    NgbaMemoryRead32(memoryAddress);

                if (i == 15)
                    cpu.r[15] = value & ~3u;
                else
                    cpu.r[i] = value;
            }
            else
            {
                NgbaMemoryWrite32(
                    memoryAddress,
                    ReadRegister(cpu, i, address)
                );
            }

            memoryAddress += 4;
        }

        if (writeBack && rn != 15)
            cpu.r[rn] = finalBase;

        return ENgbaCpuOk;
    }

    TInt ExecuteArm(
        TNgbaCpuState& cpu,
        TUint32 instruction,
        TUint32 address
    )
    {
        const TUint32 condition = instruction >> 28;

        if (!ConditionPassed(cpu.cpsr, condition))
            return ENgbaCpuOk;

        // BX.
        if ((instruction & 0x0FFFFFF0u) == 0x012FFF10u)
        {
            const TUint32 target =
                ReadRegister(cpu, instruction & 15, address);

            cpu.thumb = (target & 1u) != 0;

            cpu.r[15] = cpu.thumb
                ? target & ~1u
                : target & ~3u;

            return ENgbaCpuOk;
        }

        // Multiply / Multiply Accumulate.
        if ((instruction & 0x0FC000F0u) == 0x00000090u)
            return ExecuteMultiply(cpu, instruction);

        // Halfword and signed-byte transfers.
        if ((instruction & 0x0E000090u) == 0x00000090u)
            return ExecuteHalfwordTransfer(
                cpu,
                instruction,
                address
            );

        // Block data transfer: LDM / STM.
        if ((instruction & 0x0E000000u) == 0x08000000u)
            return ExecuteBlockTransfer(
                cpu,
                instruction,
                address
            );

        // B / BL.
        if ((instruction & 0x0E000000u) == 0x0A000000u)
        {
            TInt32 displacement =
                static_cast<TInt32>(
                    instruction & 0x00FFFFFFu
                );

            if (displacement & 0x00800000)
                displacement |=
                    static_cast<TInt32>(0xFF000000u);

            displacement *= 4;

            if (instruction & (1u << 24))
                cpu.r[14] = address + 4;

            cpu.r[15] =
                address + 8 +
                static_cast<TUint32>(displacement);

            return ENgbaCpuOk;
        }

        // LDR / STR / LDRB / STRB.
        if ((instruction & 0x0C000000u) == 0x04000000u)
        {
            const TBool pre =
                (instruction & (1u << 24)) != 0;

            const TBool add =
                (instruction & (1u << 23)) != 0;

            const TBool byte =
                (instruction & (1u << 22)) != 0;

            const TBool writeBack =
                (instruction & (1u << 21)) != 0;

            const TBool load =
                (instruction & (1u << 20)) != 0;

            const TInt rn = (instruction >> 16) & 15;
            const TInt rd = (instruction >> 12) & 15;

            if ((instruction & (1u << 25)) &&
                (instruction & (1u << 4)))
                return ENgbaCpuUnsupported;

            const TUint32 base =
                ReadRegister(cpu, rn, address);

            TUint32 offset = instruction & 0xFFFu;

            if (instruction & (1u << 25))
            {
                TBool carry = EFalse;

                offset = ShiftValue(
                    ReadRegister(
                        cpu,
                        instruction & 15,
                        address
                    ),
                    (instruction >> 5) & 3,
                    (instruction >> 7) & 31,
                    carry
                );
            }

            const TUint32 adjusted =
                add ? base + offset : base - offset;

            const TUint32 memoryAddress =
                pre ? adjusted : base;

            if (load)
            {
                const TUint32 value = byte
                    ? NgbaMemoryRead8(memoryAddress)
                    : NgbaMemoryRead32(memoryAddress);

                if (rd == 15)
                    cpu.r[15] = value & ~3u;
                else
                    cpu.r[rd] = value;
            }
            else
            {
                const TUint32 value =
                    ReadRegister(cpu, rd, address);

                if (byte)
                    NgbaMemoryWrite8(
                        memoryAddress,
                        static_cast<TUint8>(value)
                    );
                else
                    NgbaMemoryWrite32(
                        memoryAddress,
                        value
                    );
            }

            if (!pre || writeBack)
            {
                if (rn != 15)
                    cpu.r[rn] = adjusted;
            }

            return ENgbaCpuOk;
        }

        // Data processing.
        if ((instruction & 0x0C000000u) == 0)
        {
            const TInt opcode =
                (instruction >> 21) & 15;

            const TBool setFlags =
                (instruction & (1u << 20)) != 0;

            const TInt rn = (instruction >> 16) & 15;
            const TInt rd = (instruction >> 12) & 15;

            const TUint32 left =
                ReadRegister(cpu, rn, address);

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

            switch (opcode)
            {
            case 0: result = left & right; break;
            case 1: result = left ^ right; break;

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
                result = left + right +
                    ((cpu.cpsr & KFlagC) ? 1u : 0u);
                arithmetic = ETrue;
                break;

            case 6:
                result = left - right -
                    ((cpu.cpsr & KFlagC) ? 0u : 1u);
                arithmetic = ETrue;
                subtract = ETrue;
                break;

            case 7:
                result = right - left -
                    ((cpu.cpsr & KFlagC) ? 0u : 1u);
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

            case 12: result = left | right; break;
            case 13: result = right; break;
            case 14: result = left & ~right; break;
            case 15: result = ~right; break;

            default:
                return ENgbaCpuUnsupported;
            }

            if (writeResult)
            {
                if (rd == 15)
                    cpu.r[15] = result & ~3u;
                else
                    cpu.r[rd] = result;
            }

            if (setFlags || !writeResult)
            {
                if (arithmetic)
                    SetArithmeticFlags(
                        cpu,
                        left,
                        right,
                        result,
                        subtract
                    );
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

    TInt ExecuteThumb(
        TNgbaCpuState& cpu,
        TUint16 instruction,
        TUint32 address
    )
    {
        // LSL / LSR / ASR immediate.
        if ((instruction & 0xE000u) == 0x0000u)
        {
            const TInt type = (instruction >> 11) & 3;

            if (type == 3)
                return ENgbaCpuUnsupported;

            const TInt amount = (instruction >> 6) & 31;
            const TInt rs = (instruction >> 3) & 7;
            const TInt rd = instruction & 7;

            TBool carry =
                (cpu.cpsr & KFlagC) != 0;

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
            const TBool immediate =
                (instruction & (1u << 10)) != 0;

            const TBool subtract =
                (instruction & (1u << 9)) != 0;

            const TInt rd = instruction & 7;
            const TInt rn = (instruction >> 3) & 7;

            const TUint32 left = cpu.r[rn];

            const TUint32 right = immediate
                ? ((instruction >> 6) & 7)
                : cpu.r[(instruction >> 6) & 7];

            const TUint32 result = subtract
                ? left - right
                : left + right;

            cpu.r[rd] = result;

            SetArithmeticFlags(
                cpu,
                left,
                right,
                result,
                subtract
            );

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
            case 0: result = left & right; break;
            case 1: result = left ^ right; break;

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

                const TUint64 wide =
                    static_cast<TUint64>(left) +
                    right + oldCarry;

                result = static_cast<TUint32>(wide);

                SetNzFlags(cpu, result);
                cpu.cpsr &= ~(KFlagC | KFlagV);

                if (wide >> 32)
                    cpu.cpsr |= KFlagC;

                const TUint32 overflow =
                    ~(left ^ right) & (left ^ result);

                if (overflow & 0x80000000u)
                    cpu.cpsr |= KFlagV;

                return ENgbaCpuOk;
            }

            case 6:
            {
                const TUint32 oldCarry =
                    (cpu.cpsr & KFlagC) ? 1u : 0u;

                const TUint64 subtrahend =
                    static_cast<TUint64>(right) +
                    (oldCarry ? 0u : 1u);

                result = left -
                    static_cast<TUint32>(subtrahend);

                SetNzFlags(cpu, result);
                cpu.cpsr &= ~(KFlagC | KFlagV);

                if (static_cast<TUint64>(left) >= subtrahend)
                    cpu.cpsr |= KFlagC;

                const TUint32 overflow =
                    (left ^ right) & (left ^ result);

                if (overflow & 0x80000000u)
                    cpu.cpsr |= KFlagV;

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
                return ENgbaCpuOk;

            case 10:
                result = left - right;
                SetArithmeticFlags(cpu, left, right, result, ETrue);
                return ENgbaCpuOk;

            case 11:
                result = left + right;
                SetArithmeticFlags(cpu, left, right, result, EFalse);
                return ENgbaCpuOk;

            case 12: result = left | right; break;
            case 13: result = left * right; break;
            case 14: result = left & ~right; break;
            case 15: result = ~right; break;
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

            const TUint32 memoryAddress =
                cpu.r[rb] + cpu.r[ro];

            switch (operation)
            {
            case 0:
                NgbaMemoryWrite32(memoryAddress, cpu.r[rd]);
                break;

            case 1:
                NgbaMemoryWrite16(
                    memoryAddress,
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
                cpu.r[rd] = NgbaMemoryRead32(memoryAddress);
                break;

            case 5:
                cpu.r[rd] = NgbaMemoryRead16(memoryAddress);
                break;

            case 6:
                cpu.r[rd] = NgbaMemoryRead8(memoryAddress);
                break;

            case 7:
                cpu.r[rd] = static_cast<TUint32>(
                    static_cast<TInt32>(
                        static_cast<TInt16>(
                            NgbaMemoryRead16(memoryAddress)
                        )
                    )
                );
                break;
            }

            return ENgbaCpuOk;
        }

        // STR / LDR / STRB / LDRB immediate.
        if ((instruction & 0xE000u) == 0x6000u)
        {
            const TBool byte =
                (instruction & 0x1000u) != 0;

            const TBool load =
                (instruction & 0x0800u) != 0;

            const TInt offset = (instruction >> 6) & 31;
            const TInt rb = (instruction >> 3) & 7;
            const TInt rd = instruction & 7;

            const TUint32 memoryAddress =
                cpu.r[rb] + (byte ? offset : offset * 4);

            if (load)
            {
                cpu.r[rd] = byte
                    ? NgbaMemoryRead8(memoryAddress)
                    : NgbaMemoryRead32(memoryAddress);
            }
            else
            {
                if (byte)
                    NgbaMemoryWrite8(
                        memoryAddress,
                        static_cast<TUint8>(cpu.r[rd])
                    );
                else
                    NgbaMemoryWrite32(memoryAddress, cpu.r[rd]);
            }

            return ENgbaCpuOk;
        }

        // STRH / LDRH immediate.
        if ((instruction & 0xF000u) == 0x8000u)
        {
            const TBool load =
                (instruction & 0x0800u) != 0;

            const TInt offset = (instruction >> 6) & 31;
            const TInt rb = (instruction >> 3) & 7;
            const TInt rd = instruction & 7;

            const TUint32 memoryAddress =
                cpu.r[rb] + offset * 2;

            if (load)
                cpu.r[rd] = NgbaMemoryRead16(memoryAddress);
            else
                NgbaMemoryWrite16(
                    memoryAddress,
                    static_cast<TUint16>(cpu.r[rd])
                );

            return ENgbaCpuOk;
        }

        // SP-relative load/store.
        if ((instruction & 0xF000u) == 0x9000u)
        {
            const TBool load =
                (instruction & 0x0800u) != 0;

            const TInt rd = (instruction >> 8) & 7;
            const TUint32 offset =
                (instruction & 0xFFu) * 4;

            const TUint32 memoryAddress =
                cpu.r[13] + offset;

            if (load)
                cpu.r[rd] = NgbaMemoryRead32(memoryAddress);
            else
                NgbaMemoryWrite32(memoryAddress, cpu.r[rd]);

            return ENgbaCpuOk;
        }

        // Load address: PC-relative or SP-relative.
        if ((instruction & 0xF000u) == 0xA000u)
        {
            const TInt rd = (instruction >> 8) & 7;
            const TUint32 offset =
                (instruction & 0xFFu) * 4;

            if (instruction & 0x0800u)
                cpu.r[rd] = cpu.r[13] + offset;
            else
                cpu.r[rd] = ((address + 4) & ~3u) + offset;

            return ENgbaCpuOk;
        }

        // ADD / SUB SP.
        if ((instruction & 0xFF00u) == 0xB000u)
        {
            const TUint32 offset =
                (instruction & 0x7Fu) * 4;

            if (instruction & 0x0080u)
                cpu.r[13] -= offset;
            else
                cpu.r[13] += offset;

            return ENgbaCpuOk;
        }

        // PUSH / POP.
        if ((instruction & 0xF600u) == 0xB400u)
        {
            const TBool pop =
                (instruction & 0x0800u) != 0;

            const TBool extra =
                (instruction & 0x0100u) != 0;

            const TUint32 list =
                instruction & 0x00FFu;

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
                TUint32 memoryAddress =
                    cpu.r[13] - count * 4;

                for (TInt i = 0; i < 8; ++i)
                {
                    if (list & (1u << i))
                    {
                        NgbaMemoryWrite32(
                            memoryAddress,
                            cpu.r[i]
                        );

                        memoryAddress += 4;
                    }
                }

                if (extra)
                {
                    NgbaMemoryWrite32(
                        memoryAddress,
                        cpu.r[14]
                    );
                }

                cpu.r[13] -= count * 4;
            }
            else
            {
                TUint32 memoryAddress = cpu.r[13];

                for (TInt i = 0; i < 8; ++i)
                {
                    if (list & (1u << i))
                    {
                        cpu.r[i] =
                            NgbaMemoryRead32(memoryAddress);

                        memoryAddress += 4;
                    }
                }

                if (extra)
                {
                    const TUint32 value =
                        NgbaMemoryRead32(memoryAddress);

                    if (value & 1u)
                    {
                        cpu.thumb = ETrue;
                        cpu.r[15] = value & ~1u;
                    }
                    else
                    {
                        cpu.r[14] = value;
                    }

                    memoryAddress += 4;
                }

                cpu.r[13] = memoryAddress;
            }

            return ENgbaCpuOk;
        }

        // Multiple load/store: LDMIA / STMIA.
        if ((instruction & 0xF000u) == 0xC000u)
        {
            const TBool load =
                (instruction & 0x0800u) != 0;

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

                memoryAddress += 4;
            }

            cpu.r[rb] = memoryAddress;

            return ENgbaCpuOk;
        }

        // Conditional branch.
        if ((instruction & 0xF000u) == 0xD000u)
        {
            const TInt condition =
                (instruction >> 8) & 15;

            if (condition >= 14)
                return ENgbaCpuUnsupported;

            if (ConditionPassed(cpu.cpsr, condition))
            {
                TInt32 offset =
                    static_cast<TInt8>(instruction & 0xFFu);

                offset *= 2;

                cpu.r[15] =
                    address + 4 +
                    static_cast<TUint32>(offset);
            }

            return ENgbaCpuOk;
        }

        // Unconditional branch.
        if ((instruction & 0xF800u) == 0xE000u)
        {
            TInt32 offset =
                static_cast<TInt16>(
                    instruction & 0x07FFu
                );

            if (offset & 0x0400)
                offset |= static_cast<TInt16>(0xF800);

            offset *= 2;

            cpu.r[15] =
                address + 4 +
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

            cpu.r[14] =
                address + 4 +
                static_cast<TUint32>(offset * 4096);

            return ENgbaCpuOk;
        }

        // Long branch with link: second half.
        if ((instruction & 0xF800u) == 0xF800u)
        {
            const TUint32 target =
                cpu.r[14] +
                ((instruction & 0x07FFu) * 2);

            cpu.r[14] = (address + 2) | 1u;
            cpu.r[15] = target & ~1u;

            return ENgbaCpuOk;
        }

        // BX.
        if ((instruction & 0xFF87u) == 0x4700u)
        {
            const TInt rm = (instruction >> 3) & 15;

            const TUint32 target =
                ReadRegister(cpu, rm, address);

            cpu.thumb = (target & 1u) != 0;

            cpu.r[15] = cpu.thumb
                ? target & ~1u
                : target & ~3u;

            return ENgbaCpuOk;
        }

        // High-register MOV / CMP / ADD.
        if ((instruction & 0xFC00u) == 0x4400u)
        {
            const TInt opcode =
                (instruction >> 8) & 3;

            const TInt rd =
                (instruction & 7) |
                ((instruction >> 4) & 8);

            const TInt rs =
                (instruction >> 3) & 15;

            const TUint32 value =
                ReadRegister(cpu, rs, address);

            if (opcode == 0)
            {
                if (rd == 15)
                    cpu.r[15] = value & ~1u;
                else
                    cpu.r[rd] = value;
            }
            else if (opcode == 1)
            {
                const TUint32 result = cpu.r[rd] - value;

                SetArithmeticFlags(
                    cpu,
                    cpu.r[rd],
                    value,
                    result,
                    ETrue
                );
            }
            else if (opcode == 2)
            {
                if (rd == 15)
                    cpu.r[15] = value & ~1u;
                else
                    cpu.r[rd] += value;
            }
            else
            {
                return ENgbaCpuUnsupported;
            }

            return ENgbaCpuOk;
        }

        return ENgbaCpuUnsupported;
    }
}

// --------------------------------------------------
// RESET
// --------------------------------------------------

void NgbaCpuReset(TNgbaCpuState& cpu)
{
    for (TInt i = 0; i < 16; ++i)
        cpu.r[i] = 0;

    cpu.r[15] = KRomBase;
    cpu.cpsr = 0x000000D3u;

    cpu.halted = EFalse;
    cpu.thumb = EFalse;

    cpu.cycles = 0;

    cpu.lastAddress = KRomBase;
    cpu.lastOpcode = 0;
    cpu.lastInstructionSize = 0;
}

// --------------------------------------------------
// CPU STEP
// --------------------------------------------------

TInt NgbaCpuStep(TNgbaCpuState& cpu)
{
    if (!NgbaMemoryReady())
        return ENgbaCpuBadArgument;

    if (cpu.halted)
        return ENgbaCpuUnsupported;

    const TUint32 address = cpu.r[15];

    cpu.lastAddress = address;

    if (cpu.thumb)
    {
        if (address & 1u)
            return ENgbaCpuBadArgument;

        const TUint16 instruction =
            NgbaMemoryRead16(address);

        cpu.lastOpcode = instruction;
        cpu.lastInstructionSize = 2;

        cpu.r[15] = address + 2;

        const TInt result = ExecuteThumb(
            cpu,
            instruction,
            address
        );

        if (result == ENgbaCpuOk)
            ++cpu.cycles;
        else
            cpu.r[15] = address;

        return result;
    }

    if (address & 3u)
        return ENgbaCpuBadArgument;

    const TUint32 instruction =
        NgbaMemoryRead32(address);

    cpu.lastOpcode = instruction;
    cpu.lastInstructionSize = 4;

    cpu.r[15] = address + 4;

    const TInt result = ExecuteArm(
        cpu,
        instruction,
        address
    );

    if (result == ENgbaCpuOk)
        ++cpu.cycles;
    else
        cpu.r[15] = address;

    return result;
}

// --------------------------------------------------
// REGISTER ACCESS
// --------------------------------------------------

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

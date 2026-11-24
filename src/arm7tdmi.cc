
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
        default:  return EFalse;
        }
    }

    TUint32 ReadRegister(
        const TNgbaCpuState& cpu,
        TInt index,
        TUint32 instructionAddress
    )
    {
        if (index == 15)
            return instructionAddress + 8;

        return cpu.r[index & 15];
    }

    void SetNzFlags(TNgbaCpuState& cpu, TUint32 value)
    {
        cpu.cpsr &= ~(KFlagN | KFlagZ);

        if ((value & 0x80000000u) != 0)
            cpu.cpsr |= KFlagN;

        if (value == 0)
            cpu.cpsr |= KFlagZ;
    }

    TUint32 RotateRight(TUint32 value, TInt amount)
    {
        amount &= 31;

        if (amount == 0)
            return value;

        return (value >> amount) |
               (value << (32 - amount));
    }

    TUint32 DecodeOperand2(
        const TNgbaCpuState& cpu,
        TUint32 instruction,
        TUint32 instructionAddress
    )
    {
        if ((instruction & (1u << 25)) != 0)
        {
            const TInt rotate =
                static_cast<TInt>((instruction >> 8) & 15) * 2;

            return RotateRight(instruction & 0xFFu, rotate);
        }

        const TInt rm = instruction & 15;

        TUint32 value = ReadRegister(
            cpu,
            rm,
            instructionAddress
        );

        const TInt shiftType =
            static_cast<TInt>((instruction >> 5) & 3);

        const TInt shiftAmount =
            static_cast<TInt>((instruction >> 7) & 31);

        if (shiftAmount == 0)
            return value;

        switch (shiftType)
        {
        case 0:
            return value << shiftAmount;

        case 1:
            return value >> shiftAmount;

        case 2:
            if ((value & 0x80000000u) != 0)
                return (value >> shiftAmount) |
                       (~0u << (32 - shiftAmount));

            return value >> shiftAmount;

        case 3:
            return RotateRight(value, shiftAmount);
        }

        return value;
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

            if ((overflow & 0x80000000u) != 0)
                cpu.cpsr |= KFlagV;
        }
        else
        {
            const TUint64 wide =
                static_cast<TUint64>(left) +
                static_cast<TUint64>(right);

            if ((wide >> 32) != 0)
                cpu.cpsr |= KFlagC;

            const TUint32 overflow =
                ~(left ^ right) & (left ^ result);

            if ((overflow & 0x80000000u) != 0)
                cpu.cpsr |= KFlagV;
        }
    }

    TUint32 DecodeMemoryOffset(
        const TNgbaCpuState& cpu,
        TUint32 instruction,
        TUint32 instructionAddress
    )
    {
        if ((instruction & (1u << 25)) == 0)
            return instruction & 0xFFFu;

        const TInt rm = instruction & 15;
        const TUint32 value = ReadRegister(
            cpu,
            rm,
            instructionAddress
        );

        const TInt shiftType =
            static_cast<TInt>((instruction >> 5) & 3);

        const TInt shiftAmount =
            static_cast<TInt>((instruction >> 7) & 31);

        if (shiftAmount == 0)
            return value;

        switch (shiftType)
        {
        case 0:
            return value << shiftAmount;

        case 1:
            return value >> shiftAmount;

        case 2:
            if ((value & 0x80000000u) != 0)
                return (value >> shiftAmount) |
                       (~0u << (32 - shiftAmount));

            return value >> shiftAmount;

        case 3:
            return RotateRight(value, shiftAmount);
        }

        return value;
    }

    TInt ExecuteSingleDataTransfer(
        TNgbaCpuState& cpu,
        TUint32 instruction,
        TUint32 instructionAddress
    )
    {
        const TBool preIndex =
            (instruction & (1u << 24)) != 0;

        const TBool addOffset =
            (instruction & (1u << 23)) != 0;

        const TBool byteTransfer =
            (instruction & (1u << 22)) != 0;

        const TBool writeBack =
            (instruction & (1u << 21)) != 0;

        const TBool load =
            (instruction & (1u << 20)) != 0;

        const TInt rn =
            static_cast<TInt>((instruction >> 16) & 15);

        const TInt rd =
            static_cast<TInt>((instruction >> 12) & 15);

        const TUint32 base = ReadRegister(
            cpu,
            rn,
            instructionAddress
        );

        const TUint32 offset = DecodeMemoryOffset(
            cpu,
            instruction,
            instructionAddress
        );

        TUint32 adjusted = base;

        if (addOffset)
            adjusted += offset;
        else
            adjusted -= offset;

        const TUint32 address =
            preIndex ? adjusted : base;

        if (load)
        {
            TUint32 value = 0;

            if (byteTransfer)
                value = NgbaMemoryRead8(address);
            else
                value = NgbaMemoryRead32(address);

            if (rd == 15)
                cpu.r[15] = value & ~3u;
            else
                cpu.r[rd] = value;
        }
        else
        {
            const TUint32 value = ReadRegister(
                cpu,
                rd,
                instructionAddress
            );

            if (byteTransfer)
            {
                NgbaMemoryWrite8(
                    address,
                    static_cast<TUint8>(value & 0xFFu)
                );
            }
            else
            {
                NgbaMemoryWrite32(address, value);
            }
        }

        if (!preIndex || writeBack)
        {
            if (rn != 15)
                cpu.r[rn] = adjusted;
        }

        return ENgbaCpuOk;
    }
}

void NgbaCpuReset(TNgbaCpuState& cpu)
{
    for (TInt i = 0; i < 16; ++i)
        cpu.r[i] = 0;

    // Titik awal sementara untuk pengujian ROM langsung.
    // Boot BIOS GBA belum diterapkan.
    cpu.r[15] = KRomBase;

    cpu.cpsr = 0x000000D3u;
    cpu.halted = EFalse;
}

TInt NgbaCpuStep(TNgbaCpuState& cpu)
{
    if (!NgbaMemoryReady())
        return ENgbaCpuBadArgument;

    if (cpu.halted)
        return ENgbaCpuUnsupported;

    const TUint32 address = cpu.r[15];

    if ((address & 3u) != 0)
        return ENgbaCpuBadArgument;

    // Ambil instruksi melalui GBA Memory Bus.
    const TUint32 instruction =
        NgbaMemoryRead32(address);

    cpu.r[15] = address + 4;

    const TUint32 condition = instruction >> 28;

    if (!ConditionPassed(cpu.cpsr, condition))
        return ENgbaCpuOk;

    // BX Rm.
    if ((instruction & 0x0FFFFFF0u) == 0x012FFF10u)
    {
        const TInt rm = instruction & 15;

        const TUint32 target = ReadRegister(
            cpu,
            rm,
            address
        );

        if ((target & 1u) != 0)
        {
            // Thumb belum didukung.
            cpu.r[15] = target & ~1u;
            return ENgbaCpuUnsupported;
        }

        cpu.r[15] = target & ~3u;
        return ENgbaCpuOk;
    }

    // B / BL.
    if ((instruction & 0x0E000000u) == 0x0A000000u)
    {
        TInt32 displacement =
            static_cast<TInt32>(instruction & 0x00FFFFFFu);

        if ((displacement & 0x00800000) != 0)
            displacement |= static_cast<TInt32>(0xFF000000u);

        displacement <<= 2;

        if ((instruction & (1u << 24)) != 0)
            cpu.r[14] = address + 4;

        cpu.r[15] =
            address + 8 + static_cast<TUint32>(displacement);

        return ENgbaCpuOk;
    }

    // LDR / STR / LDRB / STRB.
    if ((instruction & 0x0C000000u) == 0x04000000u)
    {
        return ExecuteSingleDataTransfer(
            cpu,
            instruction,
            address
        );
    }

    // Data processing ARM.
    if ((instruction & 0x0C000000u) == 0)
    {
        const TInt opcode =
            static_cast<TInt>((instruction >> 21) & 15);

        const TBool setFlags =
            (instruction & (1u << 20)) != 0;

        const TInt rn =
            static_cast<TInt>((instruction >> 16) & 15);

        const TInt rd =
            static_cast<TInt>((instruction >> 12) & 15);

        const TUint32 left = ReadRegister(
            cpu,
            rn,
            address
        );

        const TUint32 right = DecodeOperand2(
            cpu,
            instruction,
            address
        );

        TUint32 result = 0;
        TBool writeResult = ETrue;
        TBool arithmetic = EFalse;
        TBool subtract = EFalse;

        switch (opcode)
        {
        case 0: // AND
            result = left & right;
            break;

        case 1: // EOR
            result = left ^ right;
            break;

        case 2: // SUB
            result = left - right;
            arithmetic = ETrue;
            subtract = ETrue;
            break;

        case 3: // RSB
            result = right - left;
            arithmetic = ETrue;
            subtract = ETrue;
            break;

        case 4: // ADD
            result = left + right;
            arithmetic = ETrue;
            break;

        case 10: // CMP
            result = left - right;
            arithmetic = ETrue;
            subtract = ETrue;
            writeResult = EFalse;
            break;

        case 12: // ORR
            result = left | right;
            break;

        case 13: // MOV
            result = right;
            break;

        case 14: // BIC
            result = left & (~right);
            break;

        case 15: // MVN
            result = ~right;
            break;

        default:
            cpu.r[15] = address;
            return ENgbaCpuUnsupported;
        }

        if (writeResult && rd != 15)
            cpu.r[rd] = result;

        if (rd == 15 && writeResult)
            cpu.r[15] = result & ~3u;

        if (setFlags)
        {
            if (arithmetic)
            {
                SetArithmeticFlags(
                    cpu,
                    left,
                    right,
                    result,
                    subtract
                );
            }
            else
            {
                SetNzFlags(cpu, result);
            }
        }

        return ENgbaCpuOk;
    }

    cpu.r[15] = address;
    return ENgbaCpuUnsupported;
}

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

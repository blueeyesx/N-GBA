
#include "cpu_selftest.h"

#include "arm7tdmi.h"
#include "gba_memory.h"

namespace
{
    const TUint32 KEwramTestAddress = 0x02000000u;

    // Program ARM kecil:
    // MOV r0, #5
    // ADD r1, r0, #3
    // SUB r2, r1, #2
    // STR r2, [r3]
    // LDR r4, [r3]
    // CMP r4, #6
    const TUint8 KTestRom[] =
    {
        0x05, 0x00, 0xA0, 0xE3,
        0x03, 0x10, 0x80, 0xE2,
        0x02, 0x20, 0x41, 0xE2,
        0x00, 0x20, 0x83, 0xE5,
        0x00, 0x40, 0x93, 0xE5,
        0x06, 0x00, 0x54, 0xE3
    };

    TBool CheckRegister(
        const TNgbaCpuState& cpu,
        TInt index,
        TUint32 expected
    )
    {
        return NgbaCpuGetRegister(cpu, index) == expected;
    }
}

TInt NgbaCpuRunSelfTests(
    const TDesC& aRestorePath,
    TInt aRestoreRomSize
)
{
    if (aRestoreRomSize <= 0)
        return KErrArgument;

    TInt result = NgbaMemoryInit(
        KTestRom,
        sizeof(KTestRom)
    );

    if (result == KErrNone)
    {
        TNgbaCpuState cpu;
        NgbaCpuReset(cpu);

        // r3 menunjuk ke EWRAM.
        cpu.r[3] = KEwramTestAddress;

        for (TInt i = 0; i < 6; ++i)
        {
            result = NgbaCpuStep(cpu);

            if (result != ENgbaCpuOk)
            {
                result = KErrCorrupt;
                break;
            }
        }

        if (result == ENgbaCpuOk)
        {
            if (!CheckRegister(cpu, 0, 5))
                result = KErrCorrupt;
            else if (!CheckRegister(cpu, 1, 8))
                result = KErrCorrupt;
            else if (!CheckRegister(cpu, 2, 6))
                result = KErrCorrupt;
            else if (!CheckRegister(cpu, 4, 6))
                result = KErrCorrupt;
            else if (
                NgbaMemoryRead32(KEwramTestAddress) != 6
            )
                result = KErrCorrupt;
            else if (
                (NgbaCpuGetCpsr(cpu) & 0x40000000u) == 0
            )
                result = KErrCorrupt;
        }
    }

    // Tutup mode test dan buka kembali ROM game.
    const TInt restoreResult = NgbaMemoryInitFile(
        aRestorePath,
        aRestoreRomSize
    );

    if (restoreResult != KErrNone)
        return restoreResult;

    return result;
}

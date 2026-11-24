
#ifndef NGBA_ARM7TDMI_H
#define NGBA_ARM7TDMI_H

#include <e32std.h>

struct TNgbaCpuState
{
    TUint32 r[16];
    TUint32 cpsr;

    TBool halted;

    // ETrue = Thumb 16-bit.
    // EFalse = ARM 32-bit.
    TBool thumb;

    // Jumlah instruksi yang sudah dijalankan.
    TUint32 cycles;
};

enum TNgbaCpuResult
{
    ENgbaCpuOk = 0,
    ENgbaCpuBadArgument = -1,
    ENgbaCpuRomEnd = -2,
    ENgbaCpuUnsupported = -3
};

// Mengatur CPU ke keadaan awal.
void NgbaCpuReset(TNgbaCpuState& aCpu);

// Menjalankan satu instruksi ARM atau Thumb.
TInt NgbaCpuStep(TNgbaCpuState& aCpu);

// Mendapatkan register CPU.
TUint32 NgbaCpuGetRegister(
    const TNgbaCpuState& aCpu,
    TInt aRegister
);

// Mendapatkan CPSR.
TUint32 NgbaCpuGetCpsr(
    const TNgbaCpuState& aCpu
);

// Memeriksa mode Thumb.
TBool NgbaCpuIsThumb(
    const TNgbaCpuState& aCpu
);

#endif

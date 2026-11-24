
#ifndef NGBA_ARM7TDMI_H
#define NGBA_ARM7TDMI_H

#include <e32std.h>

struct TNgbaCpuState
{
    TUint32 r[16];
    TUint32 cpsr;

    TBool halted;
    TBool thumb;

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

// Mengambil nilai register.
TUint32 NgbaCpuGetRegister(
    const TNgbaCpuState& aCpu,
    TInt aRegister
);

// Mengambil CPSR.
TUint32 NgbaCpuGetCpsr(
    const TNgbaCpuState& aCpu
);

// Memeriksa apakah CPU berada dalam mode Thumb.
TBool NgbaCpuIsThumb(
    const TNgbaCpuState& aCpu
);

#endif

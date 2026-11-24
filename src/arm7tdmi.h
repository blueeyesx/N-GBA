
#ifndef NGBA_ARM7TDMI_H
#define NGBA_ARM7TDMI_H

#include <e32std.h>

struct TNgbaCpuState
{
    TUint32 r[16];
    TUint32 cpsr;
    TBool halted;
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

// Menjalankan satu instruksi ARM melalui Memory Bus.
TInt NgbaCpuStep(TNgbaCpuState& aCpu);

TUint32 NgbaCpuGetRegister(
    const TNgbaCpuState& aCpu,
    TInt aRegister
);

TUint32 NgbaCpuGetCpsr(
    const TNgbaCpuState& aCpu
);

#endif

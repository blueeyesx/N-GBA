
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

    // Informasi instruksi terakhir untuk debugging.
    TUint32 lastAddress;
    TUint32 lastOpcode;
    TUint8 lastInstructionSize;
};

enum TNgbaCpuResult
{
    ENgbaCpuOk = 0,
    ENgbaCpuBadArgument = -1,
    ENgbaCpuRomEnd = -2,
    ENgbaCpuUnsupported = -3
};

void NgbaCpuReset(TNgbaCpuState& aCpu);

TInt NgbaCpuStep(TNgbaCpuState& aCpu);

TUint32 NgbaCpuGetRegister(
    const TNgbaCpuState& aCpu,
    TInt aRegister
);

TUint32 NgbaCpuGetCpsr(
    const TNgbaCpuState& aCpu
);

TBool NgbaCpuIsThumb(
    const TNgbaCpuState& aCpu
);

TUint32 NgbaCpuGetLastAddress(
    const TNgbaCpuState& aCpu
);

TUint32 NgbaCpuGetLastOpcode(
    const TNgbaCpuState& aCpu
);

TUint8 NgbaCpuGetLastInstructionSize(
    const TNgbaCpuState& aCpu
);

#endif

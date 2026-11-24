
#ifndef NGBA_CPU_SELFTEST_H
#define NGBA_CPU_SELFTEST_H

#include <e32std.h>

// Menguji CPU dan Memory Bus dengan instruksi ARM sederhana.
// Setelah pengujian, Memory Bus dikembalikan ke ROM game.
TInt NgbaCpuRunSelfTests(
    const TUint8* aRestoreRom,
    TInt aRestoreRomSize
);

#endif

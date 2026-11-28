
#ifndef NGBA_CPU_SELFTEST_H
#define NGBA_CPU_SELFTEST_H

#include <e32std.h>

// Menguji CPU dan Memory Bus.
// Setelah pengujian, Memory Bus dibuka kembali dari file ROM game.
TInt NgbaCpuRunSelfTests(
    const TDesC& aRestorePath,
    TInt aRestoreRomSize
);

#endif

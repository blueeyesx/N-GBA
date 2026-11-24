
#ifndef NGBA_ROM_LOADER_H
#define NGBA_ROM_LOADER_H

#include <e32base.h>

// Membuka ROM langsung dari file dan memeriksa header.
TInt NgbaLoadRom(
    const TDesC& aPath,
    TUint32& aRomSize,
    TBuf8<4>& aGameCode
);

// Pada mode file-backed, fungsi ini mengembalikan NULL.
const TUint8* NgbaRomData();

// Mengembalikan ukuran ROM.
TInt NgbaRomSize();

// Menutup ROM.
void NgbaReleaseRom();

#endif

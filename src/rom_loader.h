
#ifndef NGBA_ROM_LOADER_H
#define NGBA_ROM_LOADER_H

#include <e32base.h>

// Konfigurasi khusus setiap game.
struct TNgbaGameConfig
{
    TBuf8<4> gameCode;
    TBuf8<2> venderCode;

    TUint32 idleLoopTarget;
    TInt flashRomSize;

    TBool hasIdleLoopTarget;
    TBool found;
};

// Membuka ROM langsung dari file dan memeriksa header.
TInt NgbaLoadRom(
    const TDesC& aPath,
    TUint32& aRomSize,
    TBuf8<4>& aGameCode
);

// Mengambil konfigurasi game yang sedang dimuat.
void NgbaGetGameConfig(TNgbaGameConfig& aConfig);

// Pada mode file-backed, fungsi ini mengembalikan NULL.
const TUint8* NgbaRomData();

// Mengembalikan ukuran ROM.
TInt NgbaRomSize();

// Menutup ROM.
void NgbaReleaseRom();

#endif

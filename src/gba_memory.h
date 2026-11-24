
#ifndef NGBA_GBA_MEMORY_H
#define NGBA_GBA_MEMORY_H

#include <e32std.h>

// Mode ROM dari pointer (dipakai untuk pengujian CPU).
TInt NgbaMemoryInit(
    const TUint8* aRom,
    TInt aRomSize
);

// Mode ROM langsung dari file dengan cache kecil.
TInt NgbaMemoryInitFile(
    const TDesC& aPath,
    TInt aRomSize
);

// Menghapus dan menutup hubungan Memory Bus.
void NgbaMemoryShutdown();

// Membaca memori.
TUint8 NgbaMemoryRead8(TUint32 aAddress);
TUint16 NgbaMemoryRead16(TUint32 aAddress);
TUint32 NgbaMemoryRead32(TUint32 aAddress);

// Menulis memori.
void NgbaMemoryWrite8(TUint32 aAddress, TUint8 aValue);
void NgbaMemoryWrite16(TUint32 aAddress, TUint16 aValue);
void NgbaMemoryWrite32(TUint32 aAddress, TUint32 aValue);

// Memeriksa apakah Memory Bus aktif.
TBool NgbaMemoryReady();

#endif

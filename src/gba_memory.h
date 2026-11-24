
#ifndef NGBA_GBA_MEMORY_H
#define NGBA_GBA_MEMORY_H

#include <e32std.h>

// Inisialisasi ROM dari RAM.
TInt NgbaMemoryInit(
    const TUint8* aRom,
    TInt aRomSize
);

// Inisialisasi ROM langsung dari file.
TInt NgbaMemoryInitFile(
    const TDesC& aPath,
    TInt aRomSize
);

// Mematikan Memory Bus.
void NgbaMemoryShutdown();

// Menyimpan save game ke penyimpanan.
TInt NgbaMemorySaveFlush();

// Membaca memori.
TUint8 NgbaMemoryRead8(TUint32 aAddress);
TUint16 NgbaMemoryRead16(TUint32 aAddress);
TUint32 NgbaMemoryRead32(TUint32 aAddress);

// Menulis memori.
void NgbaMemoryWrite8(
    TUint32 aAddress,
    TUint8 aValue
);

void NgbaMemoryWrite16(
    TUint32 aAddress,
    TUint16 aValue
);

void NgbaMemoryWrite32(
    TUint32 aAddress,
    TUint32 aValue
);

// BIOS HLE: SWI 01h.
void NgbaMemoryRegisterRamReset(TUint32 aFlags);

// Status Memory Bus.
TBool NgbaMemoryReady();

#endif


#ifndef NGBA_GBA_MEMORY_H
#define NGBA_GBA_MEMORY_H

#include <e32std.h>

// --------------------------------------------------
// INISIALISASI MEMORY
// --------------------------------------------------

// Inisialisasi menggunakan data ROM di RAM.
TInt NgbaMemoryInit(
    const TUint8* aRom,
    TInt aRomSize
);

// Inisialisasi menggunakan file ROM.
// ROM dibaca melalui cache agar penggunaan RAM lebih hemat.
TInt NgbaMemoryInitFile(
    const TDesC& aPath,
    TInt aRomSize
);

// Melepaskan seluruh resource memory dan file ROM.
void NgbaMemoryShutdown();

// --------------------------------------------------
// MEMORY READ
// --------------------------------------------------

TUint8 NgbaMemoryRead8(
    TUint32 aAddress
);

TUint16 NgbaMemoryRead16(
    TUint32 aAddress
);

TUint32 NgbaMemoryRead32(
    TUint32 aAddress
);

// --------------------------------------------------
// MEMORY WRITE
// --------------------------------------------------

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

// --------------------------------------------------
// BIOS HLE
// --------------------------------------------------

// Software Interrupt 01h:
// RegisterRamReset.
//
// aFlags:
// Bit 0 = EWRAM
// Bit 1 = IWRAM
// Bit 2 = Palette RAM
// Bit 3 = VRAM
// Bit 4 = OAM
// Bit 5 = Serial registers
// Bit 6 = Sound registers
// Bit 7 = Other registers
// Bit 8 = Cartridge RAM
void NgbaMemoryRegisterRamReset(
    TUint32 aFlags
);

// --------------------------------------------------
// STATUS MEMORY
// --------------------------------------------------

// Memeriksa apakah memory bus sudah diinisialisasi.
TBool NgbaMemoryReady();

#endif // NGBA_GBA_MEMORY_H

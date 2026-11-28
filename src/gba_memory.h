
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
// INTERRUPT & DMA
// --------------------------------------------------

void NgbaMemoryRaiseIrq(TUint16 aFlag);
void NgbaMemoryRunDma(TInt aTiming);

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

// --------------------------------------------------
// KEYPAD, HALT, TIMER
// --------------------------------------------------

// Bit tombol yang SEDANG ditekan:
// 0=A 1=B 2=Select 3=Start 4=Right 5=Left 6=Up 7=Down 8=R 9=L
void NgbaMemorySetKeys(TUint16 aPressedMask);

// Mengembalikan ETrue sekali jika game menulis HALTCNT.
TBool NgbaMemoryTakeHaltRequest();

// Memajukan timer GBA sebanyak aCycles siklus CPU.
void NgbaMemoryTickTimers(TInt aCycles);

#endif // NGBA_GBA_MEMORY_H

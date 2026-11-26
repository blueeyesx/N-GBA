#ifndef GBA_MEMORY_H
#define GBA_MEMORY_H

#include <e32std.h>

TInt NgbaMemoryInit(const TUint8* aRom, TInt aRomSize);
TInt NgbaMemoryInitFile(const TDesC& aPath, TInt aRomSize);
void NgbaMemoryShutdown();

void NgbaMemoryRegisterRamReset(TUint32 aFlags);

TUint8  NgbaMemoryRead8(TUint32 aAddress);
TUint16 NgbaMemoryRead16(TUint32 aAddress);
TUint32 NgbaMemoryRead32(TUint32 aAddress);

void NgbaMemoryWrite8(TUint32 aAddress, TUint8 aValue);
void NgbaMemoryWrite16(TUint32 aAddress, TUint16 aValue);
void NgbaMemoryWrite32(TUint32 aAddress, TUint32 aValue);

TBool NgbaMemoryReady();

// Pembangkit IRQ (mengatur bit bendera di register IF 0x04000202).
void NgbaMemoryRaiseIrq(TUint16 aIrqFlag);

// Pengaturan input tombol GBA pada register KEYINPUT (0x04000130).
// Bit 0..9: A, B, Select, Start, Right, Left, Up, Down, R, L (active-low: 0=ditekan).
void NgbaMemorySetKeyInput(TUint16 aKeyMask);
void NgbaMemoryPressKey(TUint16 aKeyBit);
void NgbaMemoryReleaseKey(TUint16 aKeyBit);

#endif // GBA_MEMORY_H

#ifndef NGBA_GBA_MEMORY_H
#define NGBA_GBA_MEMORY_H

#include <e32std.h>

TInt NgbaMemoryInit(
    const TUint8* aRom,
    TInt aRomSize
);

TInt NgbaMemoryInitFile(
    const TDesC& aPath,
    TInt aRomSize
);

void NgbaMemoryShutdown();

TUint8 NgbaMemoryRead8(
    TUint32 aAddress
);

TUint16 NgbaMemoryRead16(
    TUint32 aAddress
);

TUint32 NgbaMemoryRead32(
    TUint32 aAddress
);

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

void NgbaMemoryRegisterRamReset(
    TUint32 aFlags
);

void NgbaMemoryAdvanceScanline();

TBool NgbaMemoryReady();

#endif // NGBA_GBA_MEMORY_H

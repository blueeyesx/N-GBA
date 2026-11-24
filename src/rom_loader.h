
#ifndef NGBA_ROM_LOADER_H
#define NGBA_ROM_LOADER_H

#include <e32base.h>

// Memuat ROM GBA ke RAM dan memeriksa header-nya.
TInt NgbaLoadRom(
    const TDesC& aPath,
    TUint32& aRomSize,
    TBuf8<4>& aGameCode
);

// Mengembalikan alamat data ROM yang sudah dimuat.
const TUint8* NgbaRomData();

// Mengembalikan ukuran ROM.
TInt NgbaRomSize();

// Melepaskan memori ROM.
void NgbaReleaseRom();

#endif

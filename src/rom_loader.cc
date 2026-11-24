
#include "rom_loader.h"
#include "gba_memory.h"
#include "cpu_selftest.h"

#include <e32base.h>
#include <e32std.h>
#include <f32file.h>

namespace
{
    TUint8* gRomData = NULL;
    TInt gRomSize = 0;
    TBuf8<4> gGameCode;

    const TInt KMinimumRomSize = 0xC0;
    const TInt KMaximumRomSize = 32 * 1024 * 1024;

    TBool IsValidGameCode(const TUint8* aCode)
    {
        if (aCode == NULL)
            return EFalse;

        for (TInt i = 0; i < 4; ++i)
        {
            const TUint8 c = aCode[i];

            const TBool letter =
                (c >= 'A' && c <= 'Z');

            const TBool number =
                (c >= '0' && c <= '9');

            if (!letter && !number)
                return EFalse;
        }

        return ETrue;
    }
}

void NgbaReleaseRom()
{
    NgbaMemoryShutdown();

    delete[] gRomData;

    gRomData = NULL;
    gRomSize = 0;
    gGameCode.Zero();
}

TInt NgbaLoadRom(
    const TDesC& aPath,
    TUint32& aRomSize,
    TBuf8<4>& aGameCode
)
{
    aRomSize = 0;
    aGameCode.Zero();

    NgbaReleaseRom();

    RFs fs;

    TInt error = fs.Connect();

    if (error != KErrNone)
        return error;

    RFile file;

    error = file.Open(
        fs,
        aPath,
        EFileRead | EFileShareReadersOnly
    );

    if (error != KErrNone)
    {
        fs.Close();
        return error;
    }

    TInt fileSize = 0;

    error = file.Size(fileSize);

    if (error != KErrNone)
    {
        file.Close();
        fs.Close();
        return error;
    }

    if (fileSize < KMinimumRomSize ||
        fileSize > KMaximumRomSize)
    {
        file.Close();
        fs.Close();
        return KErrCorrupt;
    }

    TUint8* buffer = new TUint8[fileSize];

    if (buffer == NULL)
    {
        file.Close();
        fs.Close();
        return KErrNoMemory;
    }

    TPtr8 descriptor(buffer, 0, fileSize);

    error = file.Read(0, descriptor, fileSize);

    file.Close();
    fs.Close();

    if (error != KErrNone)
    {
        delete[] buffer;
        return error;
    }

    if (descriptor.Length() != fileSize)
    {
        delete[] buffer;
        return KErrEof;
    }

    // Byte tetap pada header ROM GBA.
    if (buffer[0xB2] != 0x96)
    {
        delete[] buffer;
        return KErrCorrupt;
    }

    // Kode game berada pada offset 0xAC.
    if (!IsValidGameCode(buffer + 0xAC))
    {
        delete[] buffer;
        return KErrCorrupt;
    }

    gRomData = buffer;
    gRomSize = fileSize;

    gGameCode.Copy(buffer + 0xAC, 4);

    // Hubungkan ROM game ke Memory Bus.
    error = NgbaMemoryInit(
        gRomData,
        gRomSize
    );

    if (error != KErrNone)
    {
        NgbaReleaseRom();
        return error;
    }

    // Uji CPU dan RAM dengan program ARM buatan sendiri.
    // Fungsi ini mengembalikan Memory Bus ke ROM game.
    error = NgbaCpuRunSelfTests(
        gRomData,
        gRomSize
    );

    if (error != KErrNone)
    {
        NgbaReleaseRom();
        return error;
    }

    aRomSize = static_cast<TUint32>(gRomSize);
    aGameCode.Copy(gGameCode);

    return KErrNone;
}

const TUint8* NgbaRomData()
{
    return gRomData;
}

TInt NgbaRomSize()
{
    return gRomSize;
}

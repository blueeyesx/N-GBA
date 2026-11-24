
#include "rom_loader.h"
#include "gba_memory.h"
#include "cpu_selftest.h"

#include <e32std.h>
#include <f32file.h>

namespace
{
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
        return KErrTooBig;
    }

    // Hanya baca header 192 byte.
    TUint8 header[KMinimumRomSize];

    TPtr8 headerDescriptor(
        header,
        0,
        KMinimumRomSize
    );

    error = file.Read(
        0,
        headerDescriptor,
        KMinimumRomSize
    );

    file.Close();
    fs.Close();

    if (error != KErrNone)
        return error;

    if (headerDescriptor.Length() != KMinimumRomSize)
        return KErrEof;

    if (header[0xB2] != 0x96)
        return KErrCorrupt;

    if (!IsValidGameCode(header + 0xAC))
        return KErrCorrupt;

    // Buka ROM dari file menggunakan cache 4 KB.
    error = NgbaMemoryInitFile(
        aPath,
        fileSize
    );

    if (error != KErrNone)
        return error;

    // Uji CPU, lalu kembalikan Memory Bus ke file game.
    error = NgbaCpuRunSelfTests(
        aPath,
        fileSize
    );

    if (error != KErrNone)
    {
        NgbaReleaseRom();
        return error;
    }

    gRomSize = fileSize;
    gGameCode.Copy(header + 0xAC, 4);

    aRomSize = static_cast<TUint32>(gRomSize);
    aGameCode.Copy(gGameCode);

    return KErrNone;
}

const TUint8* NgbaRomData()
{
    // Tidak ada salinan ROM penuh di RAM.
    return NULL;
}

TInt NgbaRomSize()
{
    return gRomSize;
}

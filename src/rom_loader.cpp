
#include "rom_loader.h"
#include "gba_memory.h"
#include "cpu_selftest.h"
#include "game_config.h"

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

// ---------------------------------------------------------
// Melepaskan ROM yang sedang aktif
// ---------------------------------------------------------

void NgbaReleaseRom()
{
    NgbaMemoryShutdown();
    NgbaGameConfigReset();

    gRomSize = 0;
    gGameCode.Zero();
}

// ---------------------------------------------------------
// Memuat ROM GBA
// ---------------------------------------------------------

TInt NgbaLoadRom(
    const TDesC& aPath,
    TUint32& aRomSize,
    TBuf8<4>& aGameCode
)
{
    aRomSize = 0;
    aGameCode.Zero();

    NgbaReleaseRom();

    // -----------------------------------------------------
    // STAGE 1: Connect File Server
    // -----------------------------------------------------

    RFs fs;

    TInt error = fs.Connect();

    if (error != KErrNone)
        return error;

    // -----------------------------------------------------
    // STAGE 2: Open ROM
    // -----------------------------------------------------

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

    // -----------------------------------------------------
    // STAGE 3: Check ROM size
    // -----------------------------------------------------

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

    // -----------------------------------------------------
    // STAGE 4: Read ROM header
    // -----------------------------------------------------

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

    // -----------------------------------------------------
    // STAGE 5: Validate GBA header
    // -----------------------------------------------------

    if (header[0xB2] != 0x96)
        return KErrCorrupt;

    if (!IsValidGameCode(header + 0xAC))
        return KErrCorrupt;

    // -----------------------------------------------------
    // STAGE 6: Store ROM information
    // -----------------------------------------------------

    gRomSize = fileSize;

    gGameCode.Copy(
        header + 0xAC,
        4
    );

    // -----------------------------------------------------
    // STAGE 7: Load game configuration
    // -----------------------------------------------------

    error = NgbaGameConfigLoad(gGameCode);

    if (error != KErrNone)
    {
        NgbaReleaseRom();
        return error;
    }

    // -----------------------------------------------------
    // STAGE 8: Initialize file-backed ROM memory
    // -----------------------------------------------------

    error = NgbaMemoryInitFile(
        aPath,
        fileSize
    );

    if (error != KErrNone)
    {
        NgbaReleaseRom();
        return error;
    }

    // -----------------------------------------------------
    // STAGE 9: CPU self-test
    // -----------------------------------------------------

    error = NgbaCpuRunSelfTests(
        aPath,
        fileSize
    );

    if (error != KErrNone)
    {
        NgbaReleaseRom();
        return error;
    }

    // -----------------------------------------------------
    // STAGE 10: Return success
    // -----------------------------------------------------

    aRomSize = static_cast<TUint32>(gRomSize);
    aGameCode.Copy(gGameCode);

    return KErrNone;
}

// ---------------------------------------------------------
// ROM data
// ---------------------------------------------------------

const TUint8* NgbaRomData()
{
    return NULL;
}

// ---------------------------------------------------------
// ROM size
// ---------------------------------------------------------

TInt NgbaRomSize()
{
    return gRomSize;
}


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
    // Simpan data SRAM / EEPROM / Flash sebelum
    // Memory Bus dilepaskan.
    NgbaMemoryShutdown();

    // Reset konfigurasi game aktif.
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

    // Lepaskan ROM sebelumnya.
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

    // -----------------------------------------------------
    // Periksa ukuran ROM
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
    // Baca header GBA sebanyak 192 byte
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
    // Validasi Nintendo GBA header
    // -----------------------------------------------------

    // Nintendo logo check byte.
    if (header[0xB2] != 0x96)
        return KErrCorrupt;

    // Game Code berada pada offset 0xAC.
    if (!IsValidGameCode(header + 0xAC))
        return KErrCorrupt;

    // -----------------------------------------------------
    // Simpan identitas ROM
    // -----------------------------------------------------

    gRomSize = fileSize;

    gGameCode.Copy(
        header + 0xAC,
        4
    );

    // -----------------------------------------------------
    // Muat database kompatibilitas game
    // -----------------------------------------------------

    // Database:
    // C:\Data\N-GBA\game_config.txt
    //
    // Konfigurasi harus dimuat sebelum Memory Bus
    // diinisialisasi agar tipe Save dapat diterapkan.

    error = NgbaGameConfigLoad(gGameCode);

    if (error != KErrNone)
    {
        gRomSize = 0;
        gGameCode.Zero();

        NgbaGameConfigReset();

        return error;
    }

    // -----------------------------------------------------
    // Inisialisasi ROM file-backed
    // -----------------------------------------------------

    // ROM dibaca langsung dari file menggunakan cache
    // sehingga tidak perlu menyalin seluruh ROM ke RAM.

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
    // CPU self-test
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
    // Kembalikan informasi ROM ke pemanggil
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
    // ROM menggunakan sistem file-backed.
    // Tidak ada salinan ROM penuh di RAM.

    return NULL;
}

// ---------------------------------------------------------
// ROM size
// ---------------------------------------------------------

TInt NgbaRomSize()
{
    return gRomSize;
}

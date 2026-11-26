
#include "rom_loader.h"
#include "gba_memory.h"
#include "cpu_selftest.h"

#include <e32std.h>
#include <f32file.h>

namespace
{
    TInt gRomSize = 0;
    TBuf8<4> gGameCode;

    TNgbaGameConfig gGameConfig;

    const TInt KMinimumRomSize = 0xC0;
    const TInt KMaximumRomSize = 32 * 1024 * 1024;

    _LIT(KGameConfigPath, "E:\\GBA\\game_config.ini");
    _LIT(KConfigLogFolder, "E:\\GBA\\");
    _LIT(KConfigLogFile, "E:\\GBA\\ngba.log");

    void ResetGameConfig(TNgbaGameConfig& aConfig)
    {
        aConfig.gameCode.Zero();
        aConfig.venderCode.Zero();
        aConfig.idleLoopTarget = 0;
        aConfig.flashRomSize = 0;
        aConfig.hasIdleLoopTarget = EFalse;
        aConfig.found = EFalse;
    }

    void WriteConfigLog(const TDesC& aMessage)
    {
        RFs fs;

        if (fs.Connect() != KErrNone)
            return;

        TInt error = fs.MkDirAll(KConfigLogFolder);

        if (error != KErrNone &&
            error != KErrAlreadyExists)
        {
            fs.Close();
            return;
        }

        RFile file;

        error = file.Open(
            fs,
            KConfigLogFile,
            EFileWrite | EFileShareAny
        );

        if (error == KErrNotFound ||
            error == KErrPathNotFound)
        {
            error = file.Create(
                fs,
                KConfigLogFile,
                EFileWrite | EFileShareAny
            );
        }

        if (error == KErrNone)
        {
            TInt position = 0;

            error = file.Seek(ESeekEnd, position);

            if (error == KErrNone)
            {
                TBuf8<256> line;
                line.Append(_L8("N-GBA | "));

                for (TInt i = 0;
                     i < aMessage.Length() &&
                     line.Length() < 250;
                     ++i)
                {
                    const TUint16 c = aMessage[i];

                    line.Append(
                        c <= 255
                        ? static_cast<TUint8>(c)
                        : static_cast<TUint8>('?')
                    );
                }

                line.Append(_L8("\r\n"));
                file.Write(line);
            }

            file.Close();
        }

        fs.Close();
    }

    void LogGameConfigStatus()
    {
        TBuf<100> message;

        if (!gGameConfig.found)
        {
            WriteConfigLog(
                _L("GAME CONFIG NOT FOUND")
            );
            return;
        }

        message.Copy(_L("GAME CONFIG FOUND: "));

        // Konversi gameCode dari descriptor 8-bit
        // ke descriptor 16-bit secara karakter per karakter.
        for (TInt i = 0;
             i < gGameConfig.gameCode.Length();
             ++i)
        {
            message.Append(
                static_cast<TUint16>(
                    gGameConfig.gameCode[i]
                )
            );
        }

        WriteConfigLog(message);

        if (gGameConfig.flashRomSize > 0)
        {
            message.Zero();
            message.Copy(_L("FLASH TYPE: "));
            message.AppendNum(
                gGameConfig.flashRomSize / 1024
            );
            message.Append(_L("KB"));

            WriteConfigLog(message);
        }
        else
        {
            WriteConfigLog(
                _L("FLASH TYPE: NOT SPECIFIED")
            );
        }

        if (gGameConfig.hasIdleLoopTarget)
        {
            message.Zero();
            message.Copy(_L("IDLE LOOP TARGET: 0x"));
            message.AppendNum(
                static_cast<TInt>(
                    gGameConfig.idleLoopTarget
                ),
                EHex
            );

            WriteConfigLog(message);
        }
        else
        {
            WriteConfigLog(
                _L("IDLE LOOP TARGET: NOT SPECIFIED")
            );
        }
    }

    void TrimText(TPtrC8& aText)
    {
        while (aText.Length() > 0 &&
               (aText[0] == ' ' ||
                aText[0] == '\t'))
        {
            aText.Set(aText.Mid(1));
        }

        while (aText.Length() > 0 &&
               (aText[aText.Length() - 1] == ' ' ||
                aText[aText.Length() - 1] == '\t' ||
                aText[aText.Length() - 1] == '\r'))
        {
            aText.Set(aText.Left(aText.Length() - 1));
        }
    }

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

    TBool ParseHex(
        const TDesC8& aText,
        TUint32& aValue
    )
    {
        TInt start = 0;

        if (aText.Length() >= 2 &&
            aText[0] == '0' &&
            (aText[1] == 'x' || aText[1] == 'X'))
        {
            start = 2;
        }

        if (aText.Length() <= start ||
            aText.Length() - start > 8)
        {
            return EFalse;
        }

        TUint32 value = 0;

        for (TInt i = start; i < aText.Length(); ++i)
        {
            const TUint8 c = aText[i];

            TUint32 digit = 0;

            if (c >= '0' && c <= '9')
                digit = c - '0';
            else if (c >= 'A' && c <= 'F')
                digit = c - 'A' + 10;
            else if (c >= 'a' && c <= 'f')
                digit = c - 'a' + 10;
            else
                return EFalse;

            value = (value << 4) | digit;
        }

        aValue = value;
        return ETrue;
    }

    void LoadGameConfig()
    {
        ResetGameConfig(gGameConfig);

        RFs fs;

        if (fs.Connect() != KErrNone)
            return;

        RFile file;

        TInt error = file.Open(
            fs,
            KGameConfigPath,
            EFileRead | EFileShareReadersOnly
        );

        if (error != KErrNone)
        {
            fs.Close();
            return;
        }

        TBuf8<4096> data;
        error = file.Read(data);

        file.Close();
        fs.Close();

        if (error != KErrNone || data.Length() == 0)
            return;

        TBool sectionMatches = EFalse;

        TInt position = 0;

        while (position < data.Length())
        {
            TInt end = position;

            while (end < data.Length() &&
                   data[end] != '\n')
            {
                ++end;
            }

            TPtrC8 line(
                data.Ptr() + position,
                end - position
            );

            TrimText(line);

            position = end + 1;

            if (line.Length() == 0)
                continue;

            if (line[0] == '#' || line[0] == ';')
                continue;

            if (line[0] == '[')
            {
                sectionMatches = EFalse;
                continue;
            }

            const TInt equalPosition = line.Locate('=');

            if (equalPosition == KErrNotFound)
                continue;

            TPtrC8 key = line.Left(equalPosition);
            TPtrC8 value = line.Mid(equalPosition + 1);

            TrimText(key);
            TrimText(value);

            if (key.Compare(_L8("game_code")) == 0)
            {
                sectionMatches = EFalse;

                if (value.Length() == 4 &&
                    value.Compare(gGameCode) == 0)
                {
                    sectionMatches = ETrue;
                    gGameConfig.found = ETrue;
                    gGameConfig.gameCode.Copy(value);
                }

                continue;
            }

            if (!sectionMatches)
                continue;

            if (key.Compare(_L8("vender_code")) == 0)
            {
                if (value.Length() <= 2)
                    gGameConfig.venderCode.Copy(value);
            }
            else if (key.Compare(
                _L8("idle_loop_eliminate_target")) == 0)
            {
                TUint32 address = 0;

                if (ParseHex(value, address))
                {
                    gGameConfig.idleLoopTarget = address;
                    gGameConfig.hasIdleLoopTarget = ETrue;
                }
            }
            else if (key.Compare(_L8("flash_rom_type")) == 0)
            {
                if (value.Compare(_L8("128KB")) == 0)
                    gGameConfig.flashRomSize = 128 * 1024;
                else if (value.Compare(_L8("64KB")) == 0)
                    gGameConfig.flashRomSize = 64 * 1024;
                else if (value.Compare(_L8("32KB")) == 0)
                    gGameConfig.flashRomSize = 32 * 1024;
            }
        }
    }
}

void NgbaReleaseRom()
{
    NgbaMemoryShutdown();

    gRomSize = 0;
    gGameCode.Zero();

    ResetGameConfig(gGameConfig);
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

    // Baca konfigurasi khusus game.
    LoadGameConfig();

    // Catat hasil pembacaan konfigurasi.
    LogGameConfigStatus();

    return KErrNone;
}

void NgbaGetGameConfig(TNgbaGameConfig& aConfig)
{
    aConfig = gGameConfig;
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

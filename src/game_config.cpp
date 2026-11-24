
#include "game_config.h"

#include <e32base.h>
#include <e32std.h>
#include <f32file.h>

namespace
{
    const TInt KMaxConfigFileSize = 65536;
    const TInt KMaxConfigLine = 256;

    _LIT(KConfigPath, "C:\\Data\\N-GBA\\game_config.txt");

    TNgbaGameConfig gConfig;

    TBool IsSpace(TUint8 aChar)
    {
        return (aChar == ' ' ||
                aChar == '\t' ||
                aChar == '\r' ||
                aChar == '\n');
    }

    TPtrC8 Trim(const TDesC8& aText)
    {
        TInt start = 0;
        TInt end = aText.Length();

        while (start < end && IsSpace(aText[start]))
            ++start;

        while (end > start && IsSpace(aText[end - 1]))
            --end;

        return aText.Mid(start, end - start);
    }

    TBool EqualsIgnoreCase(
        const TDesC8& aLeft,
        const TDesC8& aRight)
    {
        if (aLeft.Length() != aRight.Length())
            return EFalse;

        for (TInt i = 0; i < aLeft.Length(); ++i)
        {
            TUint8 a = aLeft[i];
            TUint8 b = aRight[i];

            if (a >= 'a' && a <= 'z')
                a = static_cast<TUint8>(a - 'a' + 'A');

            if (b >= 'a' && b <= 'z')
                b = static_cast<TUint8>(b - 'a' + 'A');

            if (a != b)
                return EFalse;
        }

        return ETrue;
    }

    TBool ParseBoolean(const TDesC8& aValue)
    {
        return EqualsIgnoreCase(aValue, _L8("yes")) ||
               EqualsIgnoreCase(aValue, _L8("true")) ||
               EqualsIgnoreCase(aValue, _L8("1")) ||
               EqualsIgnoreCase(aValue, _L8("on"));
    }

    void ApplySaveType(const TDesC8& aValue)
    {
        if (EqualsIgnoreCase(aValue, _L8("SRAM")))
            gConfig.saveType = ENgbaSaveSram;
        else if (EqualsIgnoreCase(aValue, _L8("EEPROM")))
            gConfig.saveType = ENgbaSaveEeprom;
        else if (EqualsIgnoreCase(aValue, _L8("FLASH64")))
            gConfig.saveType = ENgbaSaveFlash64;
        else if (EqualsIgnoreCase(aValue, _L8("FLASH128")))
            gConfig.saveType = ENgbaSaveFlash128;
        else
            gConfig.saveType = ENgbaSaveAuto;
    }

    void ApplySetting(
        const TDesC8& aKey,
        const TDesC8& aValue)
    {
        if (EqualsIgnoreCase(aKey, _L8("save_type")))
        {
            ApplySaveType(aValue);
        }
        else if (EqualsIgnoreCase(aKey, _L8("rtc")))
        {
            gConfig.rtcEnabled = ParseBoolean(aValue);
        }
        else if (EqualsIgnoreCase(aKey, _L8("cpu_swi_hle")))
        {
            gConfig.cpuSwiHle = ParseBoolean(aValue);
        }
        else if (EqualsIgnoreCase(aKey, _L8("memory_fix")))
        {
            gConfig.memoryFix = ParseBoolean(aValue);
        }
        else if (EqualsIgnoreCase(aKey, _L8("timing_fix")))
        {
            gConfig.timingFix = ParseBoolean(aValue);
        }
        else if (EqualsIgnoreCase(aKey, _L8("dma_fix")))
        {
            gConfig.dmaFix = ParseBoolean(aValue);
        }
        else if (EqualsIgnoreCase(aKey, _L8("irq_fix")))
        {
            gConfig.irqFix = ParseBoolean(aValue);
        }
        else if (EqualsIgnoreCase(aKey, _L8("ppu_fix")))
        {
            gConfig.ppuFix = ParseBoolean(aValue);
        }
        else if (EqualsIgnoreCase(aKey, _L8("audio_fix")))
        {
            gConfig.audioFix = ParseBoolean(aValue);
        }
        else if (EqualsIgnoreCase(aKey, _L8("frameskip")))
        {
            TLex8 lex(aValue);
            TInt number = 0;

            if (lex.Val(number) == KErrNone)
            {
                if (number < 0)
                    number = 0;

                if (number > 10)
                    number = 10;

                gConfig.frameskip = number;
            }
        }
    }

    void ParseLine(
        const TDesC8& aLine,
        const TDesC8& aGameCode,
        TBool& aSectionActive)
    {
        TPtrC8 line = Trim(aLine);

        if (line.Length() == 0)
            return;

        if (line[0] == '#' || line[0] == ';')
            return;

        // Section: [BPEE]
        if (line[0] == '[')
        {
            TInt close = KErrNotFound;

            for (TInt i = 1; i < line.Length(); ++i)
            {
                if (line[i] == ']')
                {
                    close = i;
                    break;
                }
            }

            aSectionActive = EFalse;

            if (close > 1)
            {
                TPtrC8 code = line.Mid(1, close - 1);

                if (EqualsIgnoreCase(code, aGameCode))
                    aSectionActive = ETrue;
            }

            return;
        }

        if (!aSectionActive)
            return;

        TInt equal = KErrNotFound;

        for (TInt i = 0; i < line.Length(); ++i)
        {
            if (line[i] == '=')
            {
                equal = i;
                break;
            }
        }

        if (equal <= 0)
            return;

        TPtrC8 key = Trim(line.Left(equal));
        TPtrC8 value = Trim(line.Mid(equal + 1));

        ApplySetting(key, value);
    }

    void ParseDatabase(
        const TDesC8& aData,
        const TDesC8& aGameCode)
    {
        TBool sectionActive = EFalse;

        TBuf8<KMaxConfigLine> line;
        line.Zero();

        for (TInt i = 0; i < aData.Length(); ++i)
        {
            const TUint8 c = aData[i];

            if (c == '\n' || c == '\r')
            {
                if (line.Length() > 0)
                {
                    ParseLine(
                        line,
                        aGameCode,
                        sectionActive
                    );

                    line.Zero();
                }

                continue;
            }

            if (line.Length() < KMaxConfigLine)
            {
                line.Append(c);
            }
            else
            {
                // Abaikan baris yang terlalu panjang.
                line.Zero();
            }
        }

        if (line.Length() > 0)
        {
            ParseLine(
                line,
                aGameCode,
                sectionActive
            );
        }
    }
}

void NgbaGameConfigReset()
{
    gConfig.gameCode.Zero();

    gConfig.saveType = ENgbaSaveAuto;

    gConfig.rtcEnabled = EFalse;
    gConfig.cpuSwiHle = ETrue;

    gConfig.memoryFix = EFalse;
    gConfig.timingFix = EFalse;
    gConfig.dmaFix = EFalse;
    gConfig.irqFix = EFalse;
    gConfig.ppuFix = EFalse;
    gConfig.audioFix = EFalse;

    gConfig.frameskip = 0;
}

TInt NgbaGameConfigLoad(const TDesC8& aGameCode)
{
    NgbaGameConfigReset();

    if (aGameCode.Length() != 4)
        return KErrArgument;

    gConfig.gameCode.Copy(aGameCode);

    RFs fs;

    TInt error = fs.Connect();

    if (error != KErrNone)
        return KErrNone;

    RFile file;

    error = file.Open(
        fs,
        KConfigPath,
        EFileRead | EFileShareReadersOnly
    );

    if (error != KErrNone)
    {
        file.Close();
        fs.Close();

        // Database opsional.
        return KErrNone;
    }

    TInt fileSize = 0;

    error = file.Size(fileSize);

    if (error != KErrNone ||
        fileSize <= 0 ||
        fileSize > KMaxConfigFileSize)
    {
        file.Close();
        fs.Close();

        return KErrNone;
    }

    HBufC8* buffer = HBufC8::New(fileSize);

    if (buffer == NULL)
    {
        file.Close();
        fs.Close();

        return KErrNone;
    }

    TPtr8 data = buffer->Des();

    error = file.Read(data);

    file.Close();
    fs.Close();

    if (error == KErrNone)
    {
        ParseDatabase(data, aGameCode);
    }

    delete buffer;

    // Kesalahan database tidak boleh menghentikan ROM.
    return KErrNone;
}

const TNgbaGameConfig& NgbaGameConfigCurrent()
{
    return gConfig;
}

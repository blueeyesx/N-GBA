#include "window_server.h"
#include "rom_loader.h"
#include "arm7tdmi.h"
#include "gba_memory.h"
#include "ngba_paths.h"

#include <e32base.h>
#include <e32std.h>
#include <e32keys.h>
#include <f32file.h>
#include <w32std.h>

namespace {

const TInt KMainMenuCount = 8;
const TInt KSettingsMenuCount = 3;
const TInt KLanguageMenuCount = 4;
const TInt KFontMenuCount = 4;
const TInt KLogMenuCount = 2;
const TInt KMaxRoms = 24;

const TInt KCpuInstructionsPerTick = 100;
const TInt KCpuTimerInterval = 16000;

enum TLanguageMode {
    ELanguageAuto = 0,
    ELanguageIndonesian = 1,
    ELanguageEnglish = 2
};

enum TFontMode {
    EFontSmall = 0,
    EFontMedium = 1,
    EFontLarge = 2
};

enum TMenuPage {
    EMainPage = 0,
    ESettingsPage,
    ELanguagePage,
    EFontPage,
    ERomPage,
    ELogPage
};

const TUint KBackground = 0x00080D18;
const TUint KPanel = 0x00121D30;
const TUint KBlue = 0x00308CFF;
const TUint KWhite = 0x00FFFFFF;
const TUint KMuted = 0x0098A9C2;

const TInt KMenuTop = 72;

TFileName KLanguageFolder;
TFileName KLanguageFile;
TFileName KFontFile;
TFileName KLogFile;
TFileName KRomFolder;

// --------------------------------------------------
// SISTEM LOG
// --------------------------------------------------

void LogMessage(const TDesC& message)
{
    RFs fs;
    TInt error = fs.Connect();

    if (error != KErrNone) {
        return;
    }

    error = fs.MkDirAll(KLanguageFolder);

    if (error != KErrNone && error != KErrAlreadyExists) {
        fs.Close();
        return;
    }

    RFile file;

    error = file.Open(
        fs,
        KLogFile,
        EFileWrite | EFileShareAny
    );

    if (error == KErrNotFound || error == KErrPathNotFound) {
        error = file.Create(
            fs,
            KLogFile,
            EFileWrite | EFileShareAny
        );
    }

    if (error == KErrNone) {
        TInt position = 0;
        error = file.Seek(ESeekEnd, position);

        if (error == KErrNone) {
            TBuf8<512> line;
            line.Append(_L8("N-GBA | "));

            for (
                TInt i = 0;
                i < message.Length() && line.Length() < 500;
                ++i
            ) {
                const TUint16 character = message[i];

                line.Append(
                    character <= 255
                    ? static_cast<TUint8>(character)
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

void LogCpuDiagnostic(const char* diagnostic)
{
    if (diagnostic == NULL || diagnostic[0] == '\0') {
        return;
    }

    TBuf<256> message;
    message.Copy(_L("CPU DIAGNOSTIC: "));

    for (
        TInt i = 0;
        diagnostic[i] != '\0' &&
        message.Length() < message.MaxLength();
        ++i
    ) {
        message.Append(
            static_cast<TUint16>(
                static_cast<TUint8>(diagnostic[i])
            )
        );
    }

    LogMessage(message);
}

void ClearLogFile()
{
    RFs fs;

    TInt error = fs.Connect();

    if (error != KErrNone) {
        return;
    }

    RFile file;

    error = file.Replace(
        fs,
        KLogFile,
        EFileWrite | EFileShareExclusive
    );

    if (error == KErrNone) {
        file.Close();
        LogMessage(_L("LOG CLEARED"));
    }
    else {
        LogMessage(_L("FAILED TO CLEAR LOG"));
    }

    fs.Close();
}

void ReadLogFile(TBuf<512>& output)
{
    output.Zero();

    RFs fs;

    if (fs.Connect() != KErrNone) {
        output.Copy(_L("LOG FILE UNAVAILABLE"));
        return;
    }

    RFile file;

    TInt error = file.Open(
        fs,
        KLogFile,
        EFileRead | EFileShareReadersOnly
    );

    if (error != KErrNone) {
        output.Copy(_L("BELUM ADA LOG"));
        fs.Close();
        return;
    }

    TInt fileSize = 0;
    error = file.Size(fileSize);

    if (error == KErrNone && fileSize > 0) {
        const TInt readSize =
            fileSize > 480 ? 480 : fileSize;

        TInt position = fileSize - readSize;
        error = file.Seek(ESeekStart, position);

        if (error == KErrNone) {
            TBuf8<480> data;
            error = file.Read(data, readSize);

            if (error == KErrNone) {
                for (
                    TInt i = 0;
                    i < data.Length() &&
                    output.Length() < output.MaxLength();
                    ++i
                ) {
                    output.Append(
                        static_cast<TUint16>(data[i])
                    );
                }
            }
        }
    }

    file.Close();
    fs.Close();

    if (output.Length() == 0) {
        output.Copy(_L("BELUM ADA LOG"));
    }
}

// --------------------------------------------------
// FONT
// --------------------------------------------------

TInt MenuRowHeight(const TSize& size, TInt fontMode)
{
    if (size.iHeight >= 300) {
        if (fontMode == EFontLarge) return 42;
        if (fontMode == EFontMedium) return 34;
        return 27;
    }

    if (fontMode == EFontLarge) return 32;
    if (fontMode == EFontMedium) return 27;
    return 22;
}

TInt FontHeight(TInt fontMode)
{
    if (fontMode == EFontLarge) return 200;
    if (fontMode == EFontMedium) return 160;
    return 120;
}

// --------------------------------------------------
// PENGATURAN FILE
// --------------------------------------------------

TInt LoadByteSetting(
    const TDesC& fileName,
    TInt defaultValue,
    TInt minimum,
    TInt maximum
)
{
    RFs fs;

    if (fs.Connect() != KErrNone) {
        return defaultValue;
    }

    RFile file;

    TInt error = file.Open(
        fs,
        fileName,
        EFileRead | EFileShareReadersOnly
    );

    TInt value = defaultValue;

    if (error == KErrNone) {
        TBuf8<1> data;
        error = file.Read(data);

        if (error == KErrNone && data.Length() > 0) {
            TInt readValue = data[0];

            if (readValue >= minimum && readValue <= maximum) {
                value = readValue;
            }
        }

        file.Close();
    }

    fs.Close();
    return value;
}

TInt SaveByteSetting(const TDesC& fileName, TInt value)
{
    RFs fs;

    TInt error = fs.Connect();

    if (error != KErrNone) {
        return error;
    }

    error = fs.MkDirAll(KLanguageFolder);

    if (error != KErrNone && error != KErrAlreadyExists) {
        fs.Close();
        return error;
    }

    RFile file;

    error = file.Replace(
        fs,
        fileName,
        EFileWrite | EFileShareExclusive
    );

    if (error == KErrNone) {
        TBuf8<1> data;
        data.Append(static_cast<TUint8>(value));
        error = file.Write(data);
        file.Close();
    }

    fs.Close();
    return error;
}

// --------------------------------------------------
// BAHASA
// --------------------------------------------------

TInt LoadLanguageMode()
{
    return LoadByteSetting(
        KLanguageFile,
        ELanguageAuto,
        ELanguageAuto,
        ELanguageEnglish
    );
}

TInt SaveLanguageMode(TInt mode)
{
    return SaveByteSetting(KLanguageFile, mode);
}

TBool IsIndonesian(TInt mode)
{
    if (mode == ELanguageIndonesian) return ETrue;
    if (mode == ELanguageEnglish) return EFalse;

    return User::Language() == ELangIndonesian;
}

// --------------------------------------------------
// UKURAN FONT
// --------------------------------------------------

TInt LoadFontMode()
{
    return LoadByteSetting(
        KFontFile,
        EFontMedium,
        EFontSmall,
        EFontLarge
    );
}

TInt SaveFontMode(TInt mode)
{
    return SaveByteSetting(KFontFile, mode);
}

TInt AcquireFont(
    CWsScreenDevice& screen,
    TInt fontMode,
    CFont*& font
)
{
    _LIT(KTypeface, "");

    const TFontSpec fontSpec(
        KTypeface,
        FontHeight(fontMode)
    );

    return screen.GetNearestFontToDesignHeightInTwips(
        font,
        fontSpec
    );
}

// --------------------------------------------------
// ROM BROWSER
// --------------------------------------------------

TInt LoadRomList(TBuf<KMaxFileName>* names)
{
    if (names == NULL) {
        return KErrArgument;
    }

    RFs fs;

    TInt error = fs.Connect();

    if (error != KErrNone) {
        return error;
    }

    CDir* entries = NULL;

    error = fs.GetDir(
        KRomFolder,
        KEntryAttNormal,
        ESortByName,
        entries
    );

    TInt count = 0;

    if (error == KErrNone && entries != NULL) {
        for (
            TInt i = 0;
            i < entries->Count() && count < KMaxRoms;
            ++i
        ) {
            const TEntry& entry = (*entries)[i];

            if (entry.iName.Length() < 4) {
                continue;
            }

            if (entry.iName.Right(4).CompareF(_L(".gba")) != 0) {
                continue;
            }

            if (entry.iName.Length() > names[count].MaxLength()) {
                continue;
            }

            names[count].Copy(entry.iName);
            ++count;
        }
    }

    delete entries;
    fs.Close();

    if (
        error == KErrPathNotFound ||
        error == KErrNotFound
    ) {
        return KErrPathNotFound;
    }

    if (error != KErrNone) {
        return error;
    }

    return count;
}

// --------------------------------------------------
// DIAGNOSTIK CPU
// --------------------------------------------------

void FormatHex8(TUint32 value, char* output)
{
    const char* digits = "0123456789ABCDEF";

    for (TInt i = 0; i < 8; ++i) {
        const TInt shift = (7 - i) * 4;
        output[i] = digits[(value >> shift) & 0x0F];
    }

    output[8] = '\0';
}

void FormatHex2(TUint8 value, char* output)
{
    const char* digits = "0123456789ABCDEF";

    output[0] = digits[(value >> 4) & 0x0F];
    output[1] = digits[value & 0x0F];
    output[2] = '\0';
}

void AppendDiagnosticText(
    char* output,
    TInt& position,
    const char* text
)
{
    while (*text != '\0' && position < 250) {
        output[position++] = *text++;
    }
}

void AppendDiagnosticHex(
    char* output,
    TInt& position,
    TUint32 value
)
{
    char hex[9];
    FormatHex8(value, hex);

    for (TInt i = 0; i < 8 && position < 250; ++i) {
        output[position++] = hex[i];
    }
}

void AppendDiagnosticByte(
    char* output,
    TInt& position,
    TUint8 value
)
{
    char hex[3];
    FormatHex2(value, hex);

    for (TInt i = 0; i < 2 && position < 250; ++i) {
        output[position++] = hex[i];
    }
}

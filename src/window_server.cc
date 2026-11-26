
#include "window_server.h"
#include "rom_loader.h"
#include "arm7tdmi.h"
#include "gba_memory.h"
#include "gba_ppu.h"
#include "gba_screen_view.h"

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
    EGamePage,
    ELogPage
};

const TUint KBackground = 0x00080D18;
const TUint KPanel = 0x00121D30;
const TUint KBlue = 0x00308CFF;
const TUint KWhite = 0x00FFFFFF;
const TUint KMuted = 0x0098A9C2;

const TInt KMenuTop = 72;

_LIT(KLanguageFolder, "C:\\Data\\N-GBA\\");
_LIT(KLanguageFile, "C:\\Data\\N-GBA\\language.dat");
_LIT(KFontFile, "C:\\Data\\N-GBA\\font.dat");

_LIT(KLogFolder, "E:\\GBA\\");
_LIT(KLogFile, "E:\\GBA\\ngba.log");

_LIT(KRomFolder, "E:\\GBA\\");

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

    // Folder log berada di E:\GBA\
    // Jika drive E: tidak tersedia, log dilewati.
    error = fs.MkDirAll(KLogFolder);

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

    // Pengaturan bahasa dan font tetap disimpan di C:.
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

void FormatCpuDiagnostic(
    const TNgbaCpuState& cpu,
    TUint32 previousAddress,
    TUint32 previousOpcode,
    TBool hasPreviousInstruction,
    char* output
)
{
    TInt position = 0;

    AppendDiagnosticText(output, position, "PC:");
    AppendDiagnosticHex(output, position, cpu.lastAddress);

    AppendDiagnosticText(output, position, " OP:");
    AppendDiagnosticHex(output, position, cpu.lastOpcode);

    AppendDiagnosticText(
        output,
        position,
        cpu.lastInstructionSize == 2 ? " THUMB" : " ARM"
    );

    AppendDiagnosticText(
        output,
        position,
        (cpu.cpsr & 0x20u) ? " T:1\n" : " T:0\n"
    );

    // Byte ROM pada alamat instruksi.
    AppendDiagnosticText(output, position, "BYTE:");

    AppendDiagnosticByte(
        output,
        position,
        NgbaMemoryRead8(cpu.lastAddress)
    );

    AppendDiagnosticText(output, position, " ");

    AppendDiagnosticByte(
        output,
        position,
        NgbaMemoryRead8(cpu.lastAddress + 1)
    );

    AppendDiagnosticText(output, position, " ");

    AppendDiagnosticByte(
        output,
        position,
        NgbaMemoryRead8(cpu.lastAddress + 2)
    );

    AppendDiagnosticText(output, position, " ");

    AppendDiagnosticByte(
        output,
        position,
        NgbaMemoryRead8(cpu.lastAddress + 3)
    );

    output[position++] = '\n';

    // Instruksi sebelumnya.
    AppendDiagnosticText(output, position, "PREV:");

    if (hasPreviousInstruction) {
        AppendDiagnosticHex(
            output,
            position,
            previousAddress
        );

        AppendDiagnosticText(output, position, " OP:");

        AppendDiagnosticHex(
            output,
            position,
            previousOpcode
        );
    }
    else {
        AppendDiagnosticText(output, position, "NONE");
    }

    output[position++] = '\n';

    // Register.
    AppendDiagnosticText(output, position, "R0:");
    AppendDiagnosticHex(output, position, cpu.r[0]);

    AppendDiagnosticText(output, position, " R1:");
    AppendDiagnosticHex(output, position, cpu.r[1]);

    output[position++] = '\n';

    AppendDiagnosticText(output, position, "R2:");
    AppendDiagnosticHex(output, position, cpu.r[2]);

    AppendDiagnosticText(output, position, " R3:");
    AppendDiagnosticHex(output, position, cpu.r[3]);

    output[position++] = '\n';

    // CPSR.
    AppendDiagnosticText(output, position, "CPSR:");
    AppendDiagnosticHex(output, position, cpu.cpsr);

    output[position] = '\0';
}

// --------------------------------------------------
// TIMER CPU
// --------------------------------------------------

void StopCpuTimer(
    RTimer& timer,
    TRequestStatus& status,
    TBool& pending
)
{
    if (pending) {
        timer.Cancel();
        User::WaitForRequest(status);
        pending = EFalse;
    }
}

// --------------------------------------------------
// JUMLAH ITEM
// --------------------------------------------------

TInt MenuCount(TInt page, TInt romCount)
{
    if (page == ESettingsPage) return KSettingsMenuCount;
    if (page == ELanguagePage) return KLanguageMenuCount;
    if (page == EFontPage) return KFontMenuCount;
    if (page == ELogPage) return KLogMenuCount;
    if (page == EGamePage) return 1;

    if (page == ERomPage) {
        return romCount > 0 ? romCount + 1 : 1;
    }

    return KMainMenuCount;
}

// --------------------------------------------------
// TEKS MENU
// --------------------------------------------------

const char* MenuText(
    TInt page,
    TInt index,
    TBool indonesian
)
{
    if (page == EMainPage) {
        if (indonesian) {
            static const char* menu[] = {
                "PUSTAKA GAME",
                "MUAT ROM",
                "LANJUTKAN",
                "SAVE STATE",
                "PENGATURAN",
                "TENTANG N-GBA",
                "LOG",
                "KELUAR"
            };
            return menu[index];
        }

        static const char* menu[] = {
            "GAME LIBRARY",
            "LOAD ROM",
            "RESUME GAME",
            "SAVE STATES",
            "SETTINGS",
            "ABOUT N-GBA",
            "LOG",
            "EXIT"
        };
        return menu[index];
    }

    if (page == ESettingsPage) {
        if (indonesian) {
            static const char* menu[] = {
                "BAHASA",
                "UKURAN FONT",
                "KEMBALI"
            };
            return menu[index];
        }

        static const char* menu[] = {
            "LANGUAGE",
            "FONT SIZE",
            "BACK"
        };
        return menu[index];
    }

    if (page == ELanguagePage) {
        if (indonesian) {
            static const char* menu[] = {
                "OTOMATIS (SISTEM HP)",
                "BAHASA INDONESIA",
                "ENGLISH",
                "KEMBALI"
            };
            return menu[index];
        }

        static const char* menu[] = {
            "AUTO (SYSTEM)",
            "BAHASA INDONESIA",
            "ENGLISH",
            "BACK"
        };
        return menu[index];
    }

    if (page == EFontPage) {
        if (indonesian) {
            static const char* menu[] = {
                "KECIL",
                "SEDANG",
                "BESAR",
                "KEMBALI"
            };
            return menu[index];
        }

        static const char* menu[] = {
            "SMALL",
            "MEDIUM",
            "LARGE",
            "BACK"
        };
        return menu[index];
    }

    if (page == ERomPage) {
        return indonesian ? "KEMBALI" : "BACK";
    }

    if (page == ELogPage) {
        if (indonesian) {
            static const char* menu[] = {
                "HAPUS LOG",
                "KEMBALI"
            };
            return menu[index];
        }

        static const char* menu[] = {
            "CLEAR LOG",
            "BACK"
        };
        return menu[index];
    }

    return "";
}

// --------------------------------------------------
// GAMBAR
// --------------------------------------------------

void Fill(
    CWindowGc& gc,
    TInt x,
    TInt y,
    TInt width,
    TInt height,
    TUint color
)
{
    gc.SetPenStyle(CGraphicsContext::ENullPen);
    gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
    gc.SetBrushColor(TRgb(color));

    gc.DrawRect(
        TRect(x, y, x + width, y + height)
    );
}

void DrawText(
    CWindowGc& gc,
    const char* text,
    TInt x,
    TInt y,
    TUint color
)
{
    TUint16 buffer[96];
    TInt length = 0;

    while (text[length] != '\0' && length < 96) {
        buffer[length] = static_cast<TUint8>(text[length]);
        ++length;
    }

    gc.SetPenStyle(CGraphicsContext::ESolidPen);
    gc.SetPenColor(TRgb(color));

    gc.DrawText(
        TPtrC16(buffer, length),
        TPoint(x, y)
    );
}

void DrawRomText(
    CWindowGc& gc,
    const TDesC& text,
    TInt x,
    TInt y,
    TUint color
)
{
    gc.SetPenStyle(CGraphicsContext::ESolidPen);
    gc.SetPenColor(TRgb(color));
    gc.DrawText(text, TPoint(x, y));
}

// --------------------------------------------------
// GAMBAR DIAGNOSTIK CPU
// --------------------------------------------------

void DrawCpuDiagnostic(
    CWindowGc& gc,
    const char* diagnostic,
    TInt x,
    TInt y
)
{
    if (diagnostic == NULL || diagnostic[0] == '\0') {
        return;
    }

    char line[96];
    TInt lineLength = 0;
    TInt lineNumber = 0;

    const TInt lineHeight = 15;
    const TInt maxLines = 7;

    while (
        *diagnostic != '\0' &&
        lineNumber < maxLines
    ) {
        if (*diagnostic == '\n') {
            line[lineLength] = '\0';

            DrawText(
                gc,
                line,
                x,
                y + lineNumber * lineHeight,
                KWhite
            );

            ++lineNumber;
            lineLength = 0;
        }
        else if (lineLength < 95) {
            line[lineLength++] = *diagnostic;
        }

        ++diagnostic;
    }

    if (lineLength > 0 && lineNumber < maxLines) {
        line[lineLength] = '\0';

        DrawText(
            gc,
            line,
            x,
            y + lineNumber * lineHeight,
            KWhite
        );
    }
}

// --------------------------------------------------
// GAMBAR HALAMAN LOG
// --------------------------------------------------

void DrawLogPage(
    CWindowGc& gc,
    const TSize& size,
    TInt selected,
    TInt fontMode,
    TBool indonesian
)
{
    Fill(gc, 0, 0, size.iWidth, size.iHeight, KBackground);
    Fill(gc, 0, 0, size.iWidth, 4, KBlue);

    DrawText(gc, "N-GBA / LOG", 12, 30, KWhite);
    DrawText(
        gc,
        "E:\\GBA\\ngba.log",
        12,
        51,
        KBlue
    );

    TBuf<512> logText;
    ReadLogFile(logText);

    TInt starts[5];
    TInt lengths[5];

    TInt lineCount = 0;
    TInt lineStart = 0;

    for (TInt i = 0; i <= logText.Length(); ++i) {
        if (i == logText.Length() || logText[i] == '\n') {
            if (i > lineStart) {
                if (lineCount < 5) {
                    starts[lineCount] = lineStart;
                    lengths[lineCount] = i - lineStart;
                    ++lineCount;
                }
                else {
                    for (TInt j = 0; j < 4; ++j) {
                        starts[j] = starts[j + 1];
                        lengths[j] = lengths[j + 1];
                    }

                    starts[4] = lineStart;
                    lengths[4] = i - lineStart;
                }
            }

            lineStart = i + 1;
        }
    }

    const TInt lineHeight =
        fontMode == EFontLarge ? 20 :
        fontMode == EFontMedium ? 17 : 15;

    TInt y = 79;

    if (lineCount == 0) {
        DrawText(gc, "LOG KOSONG", 12, y, KMuted);
    }
    else {
        for (TInt i = 0; i < lineCount; ++i) {
            TBuf<100> line;

            TInt length = lengths[i];

            if (length > line.MaxLength()) {
                length = line.MaxLength();
            }

            line.Copy(logText.Mid(starts[i], length));

            DrawRomText(
                gc,
                line,
                8,
                y,
                KMuted
            );

            y += lineHeight;
        }
    }

    const TInt rowHeight = MenuRowHeight(size, fontMode);

    for (TInt i = 0; i < KLogMenuCount; ++i) {
        const TInt buttonY =
            size.iHeight - (2 - i) * rowHeight - 3;

        Fill(
            gc,
            8,
            buttonY,
            size.iWidth - 16,
            rowHeight - 2,
            i == selected ? KBlue : KPanel
        );

        DrawText(
            gc,
            MenuText(ELogPage, i, indonesian),
            15,
            buttonY + rowHeight / 2 + 6,
            i == selected ? KWhite : KMuted
        );
    }
}

// --------------------------------------------------
// MENGGAMBAR MENU
// --------------------------------------------------

void DrawMenu(
    CWindowGc& gc,
    CFont* font,
    const TSize& size,
    TInt page,
    TInt selected,
    TInt languageMode,
    TInt fontMode,
    const char* message,
    TBool indonesian,
    TBuf<KMaxFileName>* romNames,
    TInt romCount,
    const char* diagnostic
)
{
    if (page == ELogPage) {
        gc.UseFont(font);

        DrawLogPage(
            gc,
            size,
            selected,
            fontMode,
            indonesian
        );

        gc.DiscardFont();
        return;
    }

    Fill(gc, 0, 0, size.iWidth, size.iHeight, KBackground);
    Fill(gc, 0, 0, size.iWidth, 4, KBlue);

    gc.UseFont(font);

    DrawText(gc, "N-GBA", 12, 30, KWhite);
    DrawText(gc, "STELLAR DRAGON", 12, 51, KBlue);

    const TInt count = MenuCount(page, romCount);
    const TInt rowHeight = MenuRowHeight(size, fontMode);
    const TInt textOffset = rowHeight / 2 + 6;

    for (TInt i = 0; i < count; ++i) {
        const TInt y = KMenuTop + i * rowHeight;

        const TUint color =
            (i == selected) ? KBlue : KPanel;

        Fill(
            gc,
            8,
            y,
            size.iWidth - 16,
            rowHeight - 2,
            color
        );

        if (page == ERomPage && romCount > 0 && i < romCount) {
            DrawRomText(
                gc,
                romNames[i],
                15,
                y + textOffset,
                (i == selected) ? KWhite : KMuted
            );
        }
        else {
            const TInt textIndex =
                page == ERomPage ? 0 : i;

            DrawText(
                gc,
                MenuText(page, textIndex, indonesian),
                15,
                y + textOffset,
                (i == selected) ? KWhite : KMuted
            );
        }

        if (page == ELanguagePage && i == languageMode) {
            DrawText(
                gc,
                "*",
                size.iWidth - 18,
                y + textOffset,
                KWhite
            );
        }

        if (page == EFontPage && i == fontMode) {
            DrawText(
                gc,
                "*",
                size.iWidth - 18,
                y + textOffset,
                KWhite
            );
        }
    }

    if (diagnostic != NULL && diagnostic[0] != '\0') {
        DrawCpuDiagnostic(
            gc,
            diagnostic,
            8,
            size.iHeight - 112
        );
    }

    DrawText(
        gc,
        message,
        8,
        size.iHeight - 12,
        KBlue
    );

    gc.DiscardFont();
}

// --------------------------------------------------
// AKTIVASI MENU
// --------------------------------------------------

void ActivateSelected(
    TInt& page,
    TInt& selected,
    TInt& languageMode,
    TInt& fontMode,
    TBool& indonesian,
    TBool& running,
    const char*& message,
    TBuf<KMaxFileName>* romNames,
    TInt romCount,
    TNgbaCpuState& cpu,
    TBool& cpuActive,
    RTimer& cpuTimer,
    TRequestStatus& cpuTimerStatus,
    TBool& cpuTimerPending,
    TBool timerReady,
    char* diagnostic,
    TUint32& previousAddress,
    TUint32& previousOpcode,
    TBool& hasPreviousInstruction
)
{
    if (page == EMainPage) {
        switch (selected) {
            case 0:
            case 1:
                page = ERomPage;
                selected = 0;
                break;

            case 2:
                message = indonesian
                    ? "BELUM ADA GAME"
                    : "NO SAVED GAME YET";
                break;

            case 3:
                message = indonesian
                    ? "SAVE STATE BELUM TERSEDIA"
                    : "SAVE STATE NOT AVAILABLE";
                break;

            case 4:
                page = ESettingsPage;
                selected = 0;
                message = indonesian
                    ? "PENGATURAN"
                    : "SETTINGS";
                break;

            case 5:
                message = indonesian
                    ? "EMULATOR GBA N-GBA"
                    : "N-GBA GBA EMULATOR";
                break;

            case 6:
                page = ELogPage;
                selected = 1;
                LogMessage(_L("LOG PAGE OPENED"));
                message = indonesian
                    ? "HALAMAN LOG"
                    : "LOG PAGE";
                break;

            case 7:
                LogMessage(_L("APPLICATION EXIT"));
                running = EFalse;
                break;
        }
    }
    else if (page == ELogPage) {
        if (selected == 0) {
            ClearLogFile();

            message = indonesian
                ? "LOG DIHAPUS"
                : "LOG CLEARED";

            LogMessage(_L("LOG CLEARED BY USER"));
        }
        else {
            page = EMainPage;
            selected = 6;
        }
    }
    else if (page == ESettingsPage) {
        if (selected == 0) {
            page = ELanguagePage;
            selected = 0;
            message = indonesian
                ? "PILIH BAHASA"
                : "SELECT LANGUAGE";
        }
        else if (selected == 1) {
            page = EFontPage;
            selected = fontMode;
            message = indonesian
                ? "PILIH UKURAN FONT"
                : "SELECT FONT SIZE";
        }
        else {
            page = EMainPage;
            selected = 4;
        }
    }
    else if (page == ELanguagePage) {
        if (selected <= 2) {
            languageMode = selected;

            const TInt saveError =
                SaveLanguageMode(languageMode);

            indonesian = IsIndonesian(languageMode);

            page = ESettingsPage;
            selected = 0;

            LogMessage(
                saveError == KErrNone
                ? _L("LANGUAGE SETTING SAVED")
                : _L("LANGUAGE SETTING FAILED")
            );

            message = saveError == KErrNone
                ? (indonesian ? "BAHASA TERSIMPAN" : "LANGUAGE SAVED")
                : (indonesian ? "GAGAL SIMPAN BAHASA" : "LANGUAGE SAVE FAILED");
        }
        else {
            page = ESettingsPage;
            selected = 0;
        }
    }
    else if (page == EFontPage) {
        if (selected <= 2) {
            fontMode = selected;

            const TInt saveError =
                SaveFontMode(fontMode);

            page = ESettingsPage;
            selected = 1;

            LogMessage(
                saveError == KErrNone
                ? _L("FONT SETTING SAVED")
                : _L("FONT SETTING FAILED")
            );

            message = saveError == KErrNone
                ? (indonesian ? "UKURAN FONT TERSIMPAN" : "FONT SIZE SAVED")
                : (indonesian ? "GAGAL SIMPAN FONT" : "FONT SAVE FAILED");
        }
        else {
            page = ESettingsPage;
            selected = 1;
        }
    }
    else if (page == ERomPage) {
        if (romCount > 0 && selected < romCount) {
            StopCpuTimer(
                cpuTimer,
                cpuTimerStatus,
                cpuTimerPending
            );

            cpuActive = EFalse;

            TFileName romPath;
            romPath.Copy(KRomFolder);
            romPath.Append(romNames[selected]);

            TBuf<128> logEntry;
            logEntry.Copy(_L("ROM LOAD START: "));
            logEntry.Append(romNames[selected]);
            LogMessage(logEntry);

            TUint32 loadedSize = 0;
            TBuf8<4> gameCode;

            const TInt loadError = NgbaLoadRom(
                romPath,
                loadedSize,
                gameCode
            );

            if (loadError == KErrNone) {
                NgbaCpuReset(cpu);
                NgbaPpuReset();

                page = EGamePage;
                selected = 0;

                previousAddress = 0;
                previousOpcode = 0;
                hasPreviousInstruction = EFalse;

                if (diagnostic != NULL) {
                    diagnostic[0] = '\0';
                }

                TBuf<128> successEntry;
                successEntry.Copy(_L("ROM LOADED: "));
                successEntry.Append(romNames[selected]);
                LogMessage(successEntry);

                if (timerReady) {
                    cpuActive = ETrue;

                    cpuTimer.After(
                        cpuTimerStatus,
                        TTimeIntervalMicroSeconds32(
                            KCpuTimerInterval
                        )
                    );

                    cpuTimerPending = ETrue;

                    message = indonesian
                        ? "ROM DIMUAT - CPU BERJALAN"
                        : "ROM LOADED - CPU RUNNING";
                }
                else {
                    LogMessage(_L("CPU TIMER CREATION FAILED"));

                    message = indonesian
                        ? "ROM DIMUAT - TIMER CPU GAGAL"
                        : "ROM LOADED - CPU TIMER FAILED";
                }
            }
            else {
                TBuf<64> errorEntry;
                errorEntry.Copy(_L("ROM LOAD FAILED, ERROR: "));
                errorEntry.AppendNum(loadError);
                LogMessage(errorEntry);

                if (loadError == KErrNoMemory) {
                    message = indonesian
                        ? "RAM TIDAK CUKUP"
                        : "NOT ENOUGH MEMORY";
                }
                else {
                    message = indonesian
                        ? "GAGAL MEMUAT ROM"
                        : "ROM LOAD FAILED";
                }
            }
        }
        else {
            page = EMainPage;
            selected = 0;
            message = indonesian
                ? "KEMBALI KE MENU UTAMA"
                : "RETURN TO MAIN MENU";
        }
    }
}

// --------------------------------------------------
// KEMBALI
// --------------------------------------------------

void GoBack(
    TInt& page,
    TInt& selected,
    TBool& running
)
{
    if (page == EMainPage) {
        running = EFalse;
    }
    else if (page == ELanguagePage) {
        page = ESettingsPage;
        selected = 0;
    }
    else if (page == EFontPage) {
        page = ESettingsPage;
        selected = 1;
    }
    else if (page == EGamePage) {
        page = ERomPage;
        selected = 0;
    }
    else if (page == ERomPage) {
        page = EMainPage;
        selected = 0;
    }
    else if (page == ELogPage) {
        page = EMainPage;
        selected = 6;
    }
    else {
        page = EMainPage;
        selected = 4;
    }
}

// --------------------------------------------------
// WINDOW SERVER
// --------------------------------------------------

TInt RunWindowServer()
{
    RWsSession session;

    TInt error = session.Connect();

    if (error != KErrNone) {
        return error;
    }

    LogMessage(_L("APPLICATION STARTED"));

    {
        CWsScreenDevice screen(session);

        error = screen.Construct();

        if (error == KErrNone) {
            CWindowGc gc(&screen);

            error = gc.Construct();

        if (error == KErrNone) {

               TInt screenViewError = NgbaScreenViewInitialize();

                if (screenViewError != KErrNone) {
                    LogMessage(_L("SCREEN VIEW INITIALIZATION FAILED"));
                }
                else {
                    LogMessage(_L("SCREEN VIEW INITIALIZED"));
                }

                const TSize size = screen.SizeInPixels();

                TInt languageMode = LoadLanguageMode();
                TInt fontMode = LoadFontMode();

                TBool indonesian =
                    IsIndonesian(languageMode);

                TBuf<KMaxFileName>* romNames =
                    new TBuf<KMaxFileName>[KMaxRoms];

                if (romNames == NULL) {
                    error = KErrNoMemory;
                    LogMessage(_L("ROM LIST MEMORY ALLOCATION FAILED"));
                }
                else {
                    TInt romCount = 0;

                    CFont* font = NULL;

                    error = AcquireFont(
                        screen,
                        fontMode,
                        font
                    );

                    if (error == KErrNone) {
                        RWindowGroup group(session);

                        error = group.Construct(1, ETrue);

                        if (error == KErrNone) {
                            RWindow window(session);

                            error = window.Construct(group, 2);

                            if (error == KErrNone) {
                                TInt page = EMainPage;
                                TInt selected = 0;
                                TInt lastTappedRow = -1;

                                TBool running = ETrue;

                                const char* message =
                                    indonesian
                                    ? "ATAS BAWAH PILIH KEMBALI"
                                    : "UP DOWN SELECT BACK";

                                char diagnostic[256];
                                diagnostic[0] = '\0';

                                TUint32 previousAddress = 0;
                                TUint32 previousOpcode = 0;

                                TBool hasPreviousInstruction = EFalse;

                                TNgbaCpuState cpu;
                                NgbaCpuReset(cpu);

                                TBool cpuActive = EFalse;
                                TBool cpuTimerPending = EFalse;

                                RTimer cpuTimer;

                                const TInt timerError =
                                    cpuTimer.CreateLocal();

                                const TBool timerReady =
                                    (timerError == KErrNone);

                                if (!timerReady) {
                                    LogMessage(_L("CPU TIMER CREATION FAILED"));
                                }

                                TRequestStatus cpuTimerStatus;

                                group.SetOrdinalPosition(0);

                                window.SetExtent(
                                    TPoint(0, 0),
                                    size
                                );

                                window.SetVisible(ETrue);
                                window.Activate();

                                TRequestStatus events;
                                TRequestStatus redraws;

                                session.EventReady(&events);
                                session.RedrawReady(&redraws);

                                window.Invalidate();
                                session.Flush();

                                while (running) {
                                    User::WaitForAnyRequest();

                                    // --------------------------------
                                    // EVENT INPUT
                                    // --------------------------------

                                    if (events != KRequestPending) {
                                        if (events.Int() != KErrNone) {
                                            error = events.Int();
                                            LogMessage(_L("WINDOW EVENT ERROR"));
                                            break;
                                        }

                                        TWsEvent event;
                                        session.GetEvent(event);

                                        if (
                                            event.Type() == EEventKey ||
                                            event.Type() == EEventKeyDown ||
                                            event.Type() == EEventPointer
                                        ) {
                                            TUint key = 0;

                                            if (event.Type() == EEventPointer) {
                                                const TPointerEvent* pointer =
                                                    event.Pointer();

                                                if (
                                                    pointer != NULL &&
                                                    pointer->iType ==
                                                    TPointerEvent::EButton1Down
                                                ) {
                                                    const TInt rowHeight =
                                                        MenuRowHeight(size, fontMode);

                                                    const TInt y =
                                                        pointer->iPosition.iY;

                                                    if (page == ELogPage) {
                                                        const TInt firstButtonY =
                                                            size.iHeight -
                                                            rowHeight * 2 - 3;

                                                        if (y >= firstButtonY) {
                                                            const TInt row =
                                                                (y - firstButtonY) /
                                                                rowHeight;

                                                            if (
                                                                row >= 0 &&
                                                                row < KLogMenuCount
                                                            ) {
                                                                if (row == lastTappedRow) {
                                                                    key = EKeyEnter;
                                                                    lastTappedRow = -1;
                                                                }
                                                                else {
                                                                    selected = row;
                                                                    lastTappedRow = row;
                                                                    window.Invalidate();
                                                                }
                                                            }
                                                        }
                                                        else {
                                                            lastTappedRow = -1;
                                                        }
                                                    }
                                                    else if (y >= KMenuTop) {
                                                        const TInt row =
                                                            (y - KMenuTop) /
                                                            rowHeight;

                                                        if (
                                                            row >= 0 &&
                                                            row < MenuCount(page, romCount)
                                                        ) {
                                                            if (row == lastTappedRow) {
                                                                key = EKeyEnter;
                                                                lastTappedRow = -1;
                                                            }
                                                            else {
                                                                selected = row;
                                                                lastTappedRow = row;
                                                                window.Invalidate();
                                                            }
                                                        }
                                                        else {
                                                            lastTappedRow = -1;
                                                        }
                                                    }
                                                    else {
                                                        lastTappedRow = -1;
                                                    }
                                                }
                                            }
                                            else {
                                                key = event.Key()->iCode;
                                                lastTappedRow = -1;
                                            }

                                            if (key == EKeyUpArrow) {
                                                selected =
                                                    (selected +
                                                     MenuCount(page, romCount) - 1) %
                                                    MenuCount(page, romCount);

                                                window.Invalidate();
                                            }
                                            else if (key == EKeyDownArrow) {
                                                selected =
                                                    (selected + 1) %
                                                    MenuCount(page, romCount);

                                                window.Invalidate();
                                            }
                                            else if (
                                                key == EKeyEnter ||
                                                key == EKeyDevice3
                                            ) {
                                                const TInt oldPage = page;
                                                const TInt oldFontMode = fontMode;

                                                ActivateSelected(
                                                    page,
                                                    selected,
                                                    languageMode,
                                                    fontMode,
                                                    indonesian,
                                                    running,
                                                    message,
                                                    romNames,
                                                    romCount,
                                                    cpu,
                                                    cpuActive,
                                                    cpuTimer,
                                                    cpuTimerStatus,
                                                    cpuTimerPending,
                                                    timerReady,
                                                    diagnostic,
                                                    previousAddress,
                                                    previousOpcode,
                                                    hasPreviousInstruction
                                                );

                                                if (
                                                    oldPage == ERomPage &&
                                                    page == ERomPage &&
                                                    cpuActive
                                                ) {
                                                    diagnostic[0] = '\0';
                                                }

                                                if (
                                                    page == ERomPage &&
                                                    oldPage != ERomPage
                                                ) {
                                                    const TInt scanResult =
                                                        LoadRomList(romNames);

                                                    if (scanResult >= 0) {
                                                        romCount = scanResult;

                                                        TBuf<64> scanLog;
                                                        scanLog.Copy(_L("ROM SCAN COMPLETE: "));
                                                        scanLog.AppendNum(romCount);
                                                        LogMessage(scanLog);

                                                        if (romCount == 0) {
                                                            message = indonesian
                                                                ? "ROM GBA TIDAK DITEMUKAN"
                                                                : "NO GBA ROM FOUND";
                                                        }
                                                        else {
                                                            message = indonesian
                                                                ? "PILIH FILE ROM"
                                                                : "SELECT A ROM FILE";
                                                        }
                                                    }
                                                    else {
                                                        romCount = 0;

                                                        LogMessage(_L("ROM FOLDER NOT FOUND"));

                                                        message = indonesian
                                                            ? "BUAT FOLDER E:\\GBA"
                                                            : "CREATE FOLDER E:\\GBA";
                                                    }
                                                }

                                                if (fontMode != oldFontMode) {
                                                    CFont* newFont = NULL;

                                                    const TInt fontError =
                                                        AcquireFont(
                                                            screen,
                                                            fontMode,
                                                            newFont
                                                        );

                                                    if (fontError == KErrNone) {
                                                        screen.ReleaseFont(font);
                                                        font = newFont;
                                                    }
                                                    else {
                                                        LogMessage(_L("FONT LOAD FAILED"));

                                                        message = indonesian
                                                            ? "GAGAL MEMUAT FONT"
                                                            : "FONT LOAD FAILED";
                                                    }
                                                }

                                                window.Invalidate();
                                            }
                                            else if (
                                                key == EKeyBackspace ||
                                                key == EKeyEscape
                                            ) {
                                                GoBack(
                                                    page,
                                                    selected,
                                                    running
                                                );

                                                if (page != ERomPage && cpuActive) {
                                                    StopCpuTimer(
                                                        cpuTimer,
                                                        cpuTimerStatus,
                                                        cpuTimerPending
                                                    );

                                                    cpuActive = EFalse;
                                                    LogMessage(_L("CPU EXECUTION PAUSED"));
                                                }

                                                window.Invalidate();
                                            }
                                        }

                                        if (running) {
                                            session.EventReady(&events);
                                        }
                                    }

                                    // --------------------------------
                                    // EKSEKUSI CPU
                                    // --------------------------------

                                    if (
                                        cpuTimerPending &&
                                        cpuTimerStatus != KRequestPending
                                    ) {
                                        cpuTimerPending = EFalse;

                                        if (cpuTimerStatus.Int() != KErrNone) {
                                            cpuActive = EFalse;

                                            LogMessage(_L("CPU TIMER STOPPED"));

                                            message = indonesian
                                                ? "TIMER CPU BERHENTI"
                                                : "CPU TIMER STOPPED";

                                            window.Invalidate();
                                        }
                                            else if (cpuActive && page == EGamePage) {
                                                    previousAddress = cpu.lastAddress;
                                                    previousOpcode = cpu.lastOpcode;

                                                    hasPreviousInstruction =
                                                    cpu.lastInstructionSize == 2 ||
                                                    cpu.lastInstructionSize == 4;

                                            const TInt stepResult = NgbaPpuRunFrame(cpu);

                                       if (stepResult != ENgbaCpuOk) {
                                       cpuActive = EFalse;

                                       if (stepResult == ENgbaCpuUnsupported) {
                                           FormatCpuDiagnostic(
                                               cpu,
                                               previousAddress,
                                               previousOpcode,
                                               hasPreviousInstruction,
                                               diagnostic
                                            );

                                          LogMessage(
                                         _L("CPU STOPPED: UNSUPPORTED INSTRUCTION")
                                        );

                                          LogCpuDiagnostic(diagnostic);

                                         message = indonesian
                                         ? "CPU BERHENTI - INSTRUKSI BELUM DIDUKUNG"
                                         : "CPU STOPPED - UNSUPPORTED INSTRUCTION";
                                       }
                                       else if (stepResult == ENgbaCpuRomEnd) {
                                           LogMessage(_L("CPU REACHED ROM END"));

            message = indonesian
                ? "CPU MENCAPAI AKHIR ROM"
                : "CPU REACHED ROM END";
        }
        else {
            LogMessage(_L("CPU STOPPED: EXECUTION ERROR"));

            FormatCpuDiagnostic(
                cpu,
                previousAddress,
                previousOpcode,
                hasPreviousInstruction,
                diagnostic
            );

            LogCpuDiagnostic(diagnostic);

            message = indonesian
                ? "CPU BERHENTI - KESALAHAN"
                : "CPU STOPPED - ERROR";
        }

        window.Invalidate();
    }
    else {
        // Jadwalkan frame berikutnya.
        cpuTimer.After(
            cpuTimerStatus,
            TTimeIntervalMicroSeconds32(
                KCpuTimerInterval
            )
        );

        cpuTimerPending = ETrue;

        // Perbarui tampilan framebuffer.
        window.Invalidate();
                                       }
                                     }
                                    }

                                    // --------------------------------
                                    // REDRAW
                                    // --------------------------------

                                    if (redraws != KRequestPending) {
                                        if (redraws.Int() != KErrNone) {
                                            error = redraws.Int();
                                            LogMessage(_L("WINDOW REDRAW ERROR"));
                                            break;
                                        }

                                        TWsRedrawEvent redraw;
                                        session.GetRedraw(redraw);

                                        if (redraw.Handle() == 2) {
                                            window.BeginRedraw(redraw.Rect());
                                            gc.Activate(window);

                                            DrawMenu(
                                                gc,
                                                font,
                                                size,
                                                page,
                                                selected,
                                                languageMode,
                                                fontMode,
                                                message,
                                                indonesian,
                                                romNames,
                                                romCount,
                                                diagnostic
                                            );

                                            gc.Deactivate();
                                            window.EndRedraw();
                                        }

                                        if (running) {
                                            session.RedrawReady(&redraws);
                                        }
                                    }

                                    session.Flush();
                                }

                                // --------------------------------
                                // PEMBERSIHAN TIMER DAN EVENT
                                // --------------------------------

                                StopCpuTimer(
                                    cpuTimer,
                                    cpuTimerStatus,
                                    cpuTimerPending
                                );

                                cpuActive = EFalse;

                                if (timerReady) {
                                    cpuTimer.Close();
                                }

                                session.EventReadyCancel();
                                session.RedrawReadyCancel();

                                if (events == KRequestPending) {
                                    User::WaitForRequest(events);
                                }

                                if (redraws == KRequestPending) {
                                    User::WaitForRequest(redraws);
                                }

                                window.Close();
                            }

                            group.Close();
                        }

                        screen.ReleaseFont(font);
                    }
                    else {
                        LogMessage(_L("FONT INITIALIZATION FAILED"));
                    }

                    delete[] romNames;
                }
            }
        }
    }

    NgbaScreenViewShutdown();
    NgbaReleaseRom();

    LogMessage(_L("APPLICATION SHUTDOWN"));

    session.Close();
    return error;
}

} // namespace

// --------------------------------------------------
// APPLICATION ENTRY
// --------------------------------------------------

int GuiWindowServerMain()
{
    CTrapCleanup* cleanup = CTrapCleanup::New();

    if (cleanup == NULL) {
        return KErrNoMemory;
    }

    TInt result = KErrNone;

    TRAPD(
        trapError,
        result = RunWindowServer()
    );

    if (trapError != KErrNone) {
        result = trapError;
        LogMessage(_L("APPLICATION LEAVE ERROR"));
    }

    delete cleanup;
    return result;
}

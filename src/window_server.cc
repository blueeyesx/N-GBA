
#include "window_server.h"
#include "rom_loader.h"
#include "arm7tdmi.h"

#include <e32base.h>
#include <e32std.h>
#include <e32keys.h>
#include <f32file.h>
#include <w32std.h>

namespace {

const TInt KMainMenuCount = 7;
const TInt KSettingsMenuCount = 3;
const TInt KLanguageMenuCount = 4;
const TInt KFontMenuCount = 4;
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
    ERomPage
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
_LIT(KRomFolder, "E:\\GBA\\");

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
        for (TInt i = 0;
             i < entries->Count() && count < KMaxRoms;
             ++i) {

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

    if (error == KErrPathNotFound ||
        error == KErrNotFound) {
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

void AppendDiagnosticText(
    char* output,
    TInt& position,
    const char* text
)
{
    while (*text != '\0') {
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

    for (TInt i = 0; i < 8; ++i) {
        output[position++] = hex[i];
    }
}

void FormatCpuDiagnostic(
    const TNgbaCpuState& cpu,
    char* output
)
{
    TInt position = 0;

    // Baris 1: PC, opcode, dan mode CPU.
    AppendDiagnosticText(output, position, "PC:");
    AppendDiagnosticHex(output, position, cpu.lastAddress);

    AppendDiagnosticText(output, position, " OP:");
    AppendDiagnosticHex(output, position, cpu.lastOpcode);

    AppendDiagnosticText(
        output,
        position,
        cpu.lastInstructionSize == 2 ? " THUMB\n" : " ARM\n"
    );

    // Baris 2: R0 dan R1.
    AppendDiagnosticText(output, position, "R0:");
    AppendDiagnosticHex(output, position, cpu.r[0]);

    AppendDiagnosticText(output, position, " R1:");
    AppendDiagnosticHex(output, position, cpu.r[1]);

    output[position++] = '\n';

    // Baris 3: R2 dan R3.
    AppendDiagnosticText(output, position, "R2:");
    AppendDiagnosticHex(output, position, cpu.r[2]);

    AppendDiagnosticText(output, position, " R3:");
    AppendDiagnosticHex(output, position, cpu.r[3]);

    output[position++] = '\n';

    // Baris 4: CPSR.
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

    const TInt lineHeight = 16;

    while (*diagnostic != '\0' && lineNumber < 4) {
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

    if (lineLength > 0 && lineNumber < 4) {
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
        } else {
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

    // Diagnostik CPU ketika instruksi tidak didukung.
    if (diagnostic != NULL && diagnostic[0] != '\0') {
        DrawCpuDiagnostic(
            gc,
            diagnostic,
            8,
            size.iHeight - 78
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
    TBool timerReady
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
                running = EFalse;
                break;
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

            TUint32 loadedSize = 0;
            TBuf8<4> gameCode;

            const TInt loadError = NgbaLoadRom(
                romPath,
                loadedSize,
                gameCode
            );

            if (loadError == KErrNone) {
                NgbaCpuReset(cpu);

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
                    message = indonesian
                        ? "ROM DIMUAT - TIMER CPU GAGAL"
                        : "ROM LOADED - CPU TIMER FAILED";
                }
            }
            else if (loadError == KErrNoMemory) {
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
    else if (page == ERomPage) {
        page = EMainPage;
        selected = 0;
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

    {
        CWsScreenDevice screen(session);

        error = screen.Construct();

        if (error == KErrNone) {
            CWindowGc gc(&screen);

            error = gc.Construct();

            if (error == KErrNone) {
                const TSize size = screen.SizeInPixels();

                TInt languageMode = LoadLanguageMode();
                TInt fontMode = LoadFontMode();

                TBool indonesian =
                    IsIndonesian(languageMode);

                TBuf<KMaxFileName>* romNames =
                    new TBuf<KMaxFileName>[KMaxRoms];

                if (romNames == NULL) {
                    error = KErrNoMemory;
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

                                // Buffer diagnostik CPU.
                                char diagnostic[192];
                                diagnostic[0] = '\0';

                                // Status CPU.
                                TNgbaCpuState cpu;
                                NgbaCpuReset(cpu);

                                TBool cpuActive = EFalse;
                                TBool cpuTimerPending = EFalse;

                                RTimer cpuTimer;

                                const TInt timerError =
                                    cpuTimer.CreateLocal();

                                const TBool timerReady =
                                    (timerError == KErrNone);

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

                                                    if (y >= KMenuTop) {
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
                                                    timerReady
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

                                            message = indonesian
                                                ? "TIMER CPU BERHENTI"
                                                : "CPU TIMER STOPPED";

                                            window.Invalidate();
                                        }
                                        else if (cpuActive && page == ERomPage) {
                                            TInt stepResult = ENgbaCpuOk;

                                            for (
                                                TInt i = 0;
                                                i < KCpuInstructionsPerTick;
                                                ++i
                                            ) {
                                                stepResult = NgbaCpuStep(cpu);

                                                if (stepResult != ENgbaCpuOk) {
                                                    break;
                                                }
                                            }

                                            if (stepResult != ENgbaCpuOk) {
                                                cpuActive = EFalse;

                                                if (stepResult == ENgbaCpuUnsupported) {
                                                    FormatCpuDiagnostic(
                                                        cpu,
                                                        diagnostic
                                                    );

                                                    message = indonesian
                                                        ? "CPU BERHENTI - INSTRUKSI BELUM DIDUKUNG"
                                                        : "CPU STOPPED - UNSUPPORTED INSTRUCTION";
                                                }
                                                else if (stepResult == ENgbaCpuRomEnd) {
                                                    message = indonesian
                                                        ? "CPU MENCAPAI AKHIR ROM"
                                                        : "CPU REACHED ROM END";
                                                }
                                                else {
                                                    message = indonesian
                                                        ? "CPU BERHENTI - KESALAHAN"
                                                        : "CPU STOPPED - ERROR";
                                                }

                                                window.Invalidate();
                                            }
                                            else {
                                                cpuTimer.After(
                                                    cpuTimerStatus,
                                                    TTimeIntervalMicroSeconds32(
                                                        KCpuTimerInterval
                                                    )
                                                );

                                                cpuTimerPending = ETrue;
                                            }
                                        }
                                    }

                                    // --------------------------------
                                    // REDRAW
                                    // --------------------------------

                                    if (redraws != KRequestPending) {
                                        if (redraws.Int() != KErrNone) {
                                            error = redraws.Int();
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

                    delete[] romNames;
                }
            }
        }
    }

    NgbaReleaseRom();

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
    }

    delete cleanup;
    return result;
}


#include "window_server.h"

#include <e32base.h>
#include <e32std.h>
#include <e32keys.h>
#include <f32file.h>
#include <w32std.h>

namespace {

// --------------------------------------------------
// JUMLAH MENU
// --------------------------------------------------

const TInt KMainMenuCount = 7;
const TInt KSettingsMenuCount = 3;
const TInt KLanguageMenuCount = 4;
const TInt KFontMenuCount = 4;

// --------------------------------------------------
// MODE BAHASA
// --------------------------------------------------

enum TLanguageMode {
    ELanguageAuto = 0,
    ELanguageIndonesian = 1,
    ELanguageEnglish = 2
};

// --------------------------------------------------
// MODE FONT
// --------------------------------------------------

enum TFontMode {
    EFontSmall = 0,
    EFontMedium = 1,
    EFontLarge = 2
};

// --------------------------------------------------
// HALAMAN
// --------------------------------------------------

enum TMenuPage {
    EMainPage = 0,
    ESettingsPage,
    ELanguagePage,
    EFontPage
};

// --------------------------------------------------
// WARNA
// --------------------------------------------------

const TUint KBackground = 0x00080D18;
const TUint KPanel      = 0x00121D30;
const TUint KBlue       = 0x00308CFF;
const TUint KWhite      = 0x00FFFFFF;
const TUint KMuted      = 0x0098A9C2;

const TInt KMenuTop = 72;

// --------------------------------------------------
// FILE PENGATURAN
// --------------------------------------------------

_LIT(KLanguageFolder, "C:\\Data\\N-GBA\\");

_LIT(
    KLanguageFile,
    "C:\\Data\\N-GBA\\language.dat"
);

_LIT(
    KFontFile,
    "C:\\Data\\N-GBA\\font.dat"
);

// --------------------------------------------------
// TINGGI BARIS MENU
// --------------------------------------------------

TInt MenuRowHeight(
    const TSize& size,
    TInt fontMode
)
{
    if (size.iHeight >= 300) {

        if (fontMode == EFontLarge) {
            return 42;
        }

        if (fontMode == EFontMedium) {
            return 34;
        }

        return 27;
    }

    if (fontMode == EFontLarge) {
        return 32;
    }

    if (fontMode == EFontMedium) {
        return 27;
    }

    return 22;
}

// --------------------------------------------------
// UKURAN FONT TWIPS
// --------------------------------------------------

TInt FontHeight(TInt fontMode)
{
    if (fontMode == EFontLarge) {
        return 200;
    }

    if (fontMode == EFontMedium) {
        return 160;
    }

    return 120;
}

// --------------------------------------------------
// MEMBACA PENGATURAN SATU BYTE
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

        if (
            error == KErrNone &&
            data.Length() > 0
        ) {
            const TInt readValue = data[0];

            if (
                readValue >= minimum &&
                readValue <= maximum
            ) {
                value = readValue;
            }
        }

        file.Close();
    }

    fs.Close();

    return value;
}

// --------------------------------------------------
// MENYIMPAN PENGATURAN SATU BYTE
// --------------------------------------------------

TInt SaveByteSetting(
    const TDesC& fileName,
    TInt value
)
{
    RFs fs;

    TInt error = fs.Connect();

    if (error != KErrNone) {
        return error;
    }

    error = fs.MkDirAll(KLanguageFolder);

    if (
        error != KErrNone &&
        error != KErrAlreadyExists
    ) {
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

        data.Append(
            static_cast<TUint8>(value)
        );

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
    return SaveByteSetting(
        KLanguageFile,
        mode
    );
}

TBool IsIndonesian(TInt mode)
{
    if (mode == ELanguageIndonesian) {
        return ETrue;
    }

    if (mode == ELanguageEnglish) {
        return EFalse;
    }

    return User::Language() == ELangIndonesian;
}

// --------------------------------------------------
// FONT
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
    return SaveByteSetting(
        KFontFile,
        mode
    );
}

// --------------------------------------------------
// MENGAMBIL FONT SESUAI UKURAN
// --------------------------------------------------

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
// JUMLAH ITEM MENU
// --------------------------------------------------

TInt MenuCount(TInt page)
{
    if (page == ESettingsPage) {
        return KSettingsMenuCount;
    }

    if (page == ELanguagePage) {
        return KLanguageMenuCount;
    }

    if (page == EFontPage) {
        return KFontMenuCount;
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

    return "";
}

// --------------------------------------------------
// GRAFIK
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
    gc.SetPenStyle(
        CGraphicsContext::ENullPen
    );

    gc.SetBrushStyle(
        CGraphicsContext::ESolidBrush
    );

    gc.SetBrushColor(
        TRgb(color)
    );

    gc.DrawRect(
        TRect(
            x,
            y,
            x + width,
            y + height
        )
    );
}

// --------------------------------------------------
// MENULIS TEKS
// --------------------------------------------------

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

    while (
        text[length] != '\0' &&
        length < 96
    ) {
        buffer[length] =
            static_cast<TUint8>(text[length]);

        ++length;
    }

    gc.SetPenStyle(
        CGraphicsContext::ESolidPen
    );

    gc.SetPenColor(
        TRgb(color)
    );

    gc.DrawText(
        TPtrC16(buffer, length),
        TPoint(x, y)
    );
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
    TBool indonesian
)
{
    Fill(
        gc,
        0,
        0,
        size.iWidth,
        size.iHeight,
        KBackground
    );

    Fill(
        gc,
        0,
        0,
        size.iWidth,
        4,
        KBlue
    );

    gc.UseFont(font);

    DrawText(
        gc,
        "N-GBA",
        12,
        30,
        KWhite
    );

    DrawText(
        gc,
        "STELLAR DRAGON",
        12,
        51,
        KBlue
    );

    const TInt count = MenuCount(page);

    const TInt rowHeight =
        MenuRowHeight(size, fontMode);

    const TInt textOffset =
        rowHeight / 2 + 6;

    for (TInt i = 0; i < count; ++i) {

        const TInt y =
            KMenuTop + i * rowHeight;

        const TUint color =
            (i == selected)
            ? KBlue
            : KPanel;

        Fill(
            gc,
            8,
            y,
            size.iWidth - 16,
            rowHeight - 2,
            color
        );

        DrawText(
            gc,
            MenuText(
                page,
                i,
                indonesian
            ),
            15,
            y + textOffset,
            (i == selected)
            ? KWhite
            : KMuted
        );

        // Tanda bahasa aktif
        if (
            page == ELanguagePage &&
            i == languageMode
        ) {
            DrawText(
                gc,
                "*",
                size.iWidth - 18,
                y + textOffset,
                KWhite
            );
        }

        // Tanda font aktif
        if (
            page == EFontPage &&
            i == fontMode
        ) {
            DrawText(
                gc,
                "*",
                size.iWidth - 18,
                y + textOffset,
                KWhite
            );
        }
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
    const char*& message
)
{
    if (page == EMainPage) {

        switch (selected) {

            case 0:
                message =
                    indonesian
                    ? "PUSTAKA GAME DIPILIH"
                    : "GAME LIBRARY SELECTED";
                break;

            case 1:
                message =
                    indonesian
                    ? "MUAT ROM DIPILIH"
                    : "LOAD ROM SELECTED";
                break;

            case 2:
                message =
                    indonesian
                    ? "BELUM ADA GAME"
                    : "NO SAVED GAME YET";
                break;

            case 3:
                message =
                    indonesian
                    ? "SAVE STATE DIPILIH"
                    : "SAVE STATES SELECTED";
                break;

            case 4:
                page = ESettingsPage;
                selected = 0;

                message =
                    indonesian
                    ? "PENGATURAN"
                    : "SETTINGS";
                break;

            case 5:
                message =
                    indonesian
                    ? "EMULATOR GBA N-GBA"
                    : "N-GBA GBA EMULATOR";
                break;

            case 6:
                running = EFalse;
                break;
        }

    } else if (page == ESettingsPage) {

        if (selected == 0) {

            page = ELanguagePage;
            selected = 0;

            message =
                indonesian
                ? "PILIH BAHASA"
                : "SELECT LANGUAGE";

        } else if (selected == 1) {

            page = EFontPage;
            selected = fontMode;

            message =
                indonesian
                ? "PILIH UKURAN FONT"
                : "SELECT FONT SIZE";

        } else {

            page = EMainPage;
            selected = 4;

            message =
                indonesian
                ? "PENGATURAN"
                : "SETTINGS";
        }

    } else if (page == ELanguagePage) {

        if (selected <= 2) {

            languageMode = selected;

            const TInt saveError =
                SaveLanguageMode(languageMode);

            indonesian =
                IsIndonesian(languageMode);

            page = ESettingsPage;
            selected = 0;

            if (saveError == KErrNone) {

                message =
                    indonesian
                    ? "BAHASA TERSIMPAN"
                    : "LANGUAGE SAVED";

            } else {

                message =
                    indonesian
                    ? "GAGAL SIMPAN BAHASA"
                    : "LANGUAGE SAVE FAILED";
            }

        } else {

            page = ESettingsPage;
            selected = 0;

            message =
                indonesian
                ? "PENGATURAN"
                : "SETTINGS";
        }

    } else if (page == EFontPage) {

        if (selected <= 2) {

            fontMode = selected;

            const TInt saveError =
                SaveFontMode(fontMode);

            page = ESettingsPage;
            selected = 1;

            if (saveError == KErrNone) {

                message =
                    indonesian
                    ? "UKURAN FONT TERSIMPAN"
                    : "FONT SIZE SAVED";

            } else {

                message =
                    indonesian
                    ? "GAGAL SIMPAN FONT"
                    : "FONT SAVE FAILED";
            }

        } else {

            page = ESettingsPage;
            selected = 1;

            message =
                indonesian
                ? "PENGATURAN"
                : "SETTINGS";
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

    } else if (page == ELanguagePage) {

        page = ESettingsPage;
        selected = 0;

    } else if (page == EFontPage) {

        page = ESettingsPage;
        selected = 1;

    } else {

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

                const TSize size =
                    screen.SizeInPixels();

                // ----------------------------------
                // MEMUAT PENGATURAN
                // ----------------------------------

                TInt languageMode =
                    LoadLanguageMode();

                TInt fontMode =
                    LoadFontMode();

                TBool indonesian =
                    IsIndonesian(languageMode);

                // ----------------------------------
                // MEMUAT FONT
                // ----------------------------------

                CFont* font = NULL;

                error = AcquireFont(
                    screen,
                    fontMode,
                    font
                );

                if (error == KErrNone) {

                    RWindowGroup group(session);

                    error = group.Construct(
                        1,
                        ETrue
                    );

                    if (error == KErrNone) {

                        RWindow window(session);

                        error = window.Construct(
                            group,
                            2
                        );

                        if (error == KErrNone) {

                            TInt page = EMainPage;
                            TInt selected = 0;

                            TInt lastTappedRow = -1;

                            TBool running = ETrue;

                            const char* message =
                                indonesian
                                ? "ATAS BAWAH PILIH KEMBALI"
                                : "UP DOWN SELECT BACK";

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

                            // ----------------------------------
                            // EVENT LOOP
                            // ----------------------------------

                            while (running) {

                                User::WaitForAnyRequest();

                                // --------------------------
                                // INPUT
                                // --------------------------

                                if (
                                    events != KRequestPending
                                ) {

                                    if (
                                        events.Int() != KErrNone
                                    ) {
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

                                        // ------------------
                                        // TOUCHSCREEN
                                        // ------------------

                                        if (
                                            event.Type() ==
                                            EEventPointer
                                        ) {

                                            const TPointerEvent* pointer =
                                                event.Pointer();

                                            if (
                                                pointer != NULL &&
                                                pointer->iType ==
                                                TPointerEvent::EButton1Down
                                            ) {

                                                const TInt rowHeight =
                                                    MenuRowHeight(
                                                        size,
                                                        fontMode
                                                    );

                                                const TInt y =
                                                    pointer->iPosition.iY;

                                                if (y >= KMenuTop) {

                                                    const TInt row =
                                                        (y - KMenuTop) /
                                                        rowHeight;

                                                    if (
                                                        row >= 0 &&
                                                        row < MenuCount(page)
                                                    ) {

                                                        if (
                                                            row ==
                                                            lastTappedRow
                                                        ) {

                                                            // Ketukan kedua
                                                            key = EKeyEnter;

                                                            lastTappedRow = -1;

                                                        } else {

                                                            // Ketukan pertama
                                                            selected = row;

                                                            lastTappedRow = row;

                                                            window.Invalidate();
                                                        }

                                                    } else {

                                                        lastTappedRow = -1;
                                                    }

                                                } else {

                                                    lastTappedRow = -1;
                                                }
                                            }

                                        } else {

                                            // ------------------
                                            // TOMBOL FISIK
                                            // ------------------

                                            key =
                                                event.Key()->iCode;

                                            lastTappedRow = -1;
                                        }

                                        // ------------------
                                        // ATAS
                                        // ------------------

                                        if (
                                            key == EKeyUpArrow
                                        ) {

                                            selected =
                                                (
                                                    selected +
                                                    MenuCount(page) - 1
                                                ) %
                                                MenuCount(page);

                                            window.Invalidate();

                                        // ------------------
                                        // BAWAH
                                        // ------------------

                                        } else if (
                                            key == EKeyDownArrow
                                        ) {

                                            selected =
                                                (
                                                    selected + 1
                                                ) %
                                                MenuCount(page);

                                            window.Invalidate();

                                        // ------------------
                                        // ENTER
                                        // ------------------

                                        } else if (
                                            key == EKeyEnter ||
                                            key == EKeyDevice3
                                        ) {

                                            const TInt oldFontMode =
                                                fontMode;

                                            ActivateSelected(
                                                page,
                                                selected,
                                                languageMode,
                                                fontMode,
                                                indonesian,
                                                running,
                                                message
                                            );

                                            // Perbarui font jika berubah
                                            if (
                                                fontMode != oldFontMode
                                            ) {

                                                CFont* newFont = NULL;

                                                const TInt fontError =
                                                    AcquireFont(
                                                        screen,
                                                        fontMode,
                                                        newFont
                                                    );

                                                if (
                                                    fontError == KErrNone
                                                ) {

                                                    screen.ReleaseFont(font);

                                                    font = newFont;

                                                } else {

                                                    message =
                                                        indonesian
                                                        ? "GAGAL MEMUAT FONT"
                                                        : "FONT LOAD FAILED";
                                                }
                                            }

                                            window.Invalidate();

                                        // ------------------
                                        // BACK
                                        // ------------------

                                        } else if (
                                            key == EKeyBackspace ||
                                            key == EKeyEscape
                                        ) {

                                            GoBack(
                                                page,
                                                selected,
                                                running
                                            );

                                            window.Invalidate();
                                        }
                                    }

                                    if (running) {
                                        session.EventReady(&events);
                                    }
                                }

                                // --------------------------
                                // REDRAW
                                // --------------------------

                                if (
                                    redraws != KRequestPending
                                ) {

                                    if (
                                        redraws.Int() != KErrNone
                                    ) {
                                        error = redraws.Int();
                                        break;
                                    }

                                    TWsRedrawEvent redraw;

                                    session.GetRedraw(redraw);

                                    if (redraw.Handle() == 2) {

                                        window.BeginRedraw(
                                            redraw.Rect()
                                        );

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
                                            indonesian
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

                            // --------------------------
                            // CLEANUP EVENT
                            // --------------------------

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
            }
        }
    }

    session.Close();

    return error;
}

} // namespace

// --------------------------------------------------
// APPLICATION ENTRY
// --------------------------------------------------

int GuiWindowServerMain()
{
    CTrapCleanup* cleanup =
        CTrapCleanup::New();

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


#include "window_server.h"

#include <e32base.h>
#include <e32std.h>
#include <e32keys.h>
#include <f32file.h>
#include <w32std.h>

namespace {

// Menu utama
const TInt KMainMenuCount = 7;

// Menu pengaturan
const TInt KSettingsMenuCount = 2;

// Menu bahasa
const TInt KLanguageMenuCount = 4;

// Mode bahasa
enum TLanguageMode {
    ELanguageAuto = 0,
    ELanguageIndonesian = 1,
    ELanguageEnglish = 2
};

// Halaman aplikasi
enum TMenuPage {
    EMainPage = 0,
    ESettingsPage,
    ELanguagePage
};

const TUint KBackground = 0x00080D18;
const TUint KPanel      = 0x00121D30;
const TUint KBlue       = 0x00308CFF;
const TUint KWhite      = 0x00FFFFFF;
const TUint KMuted      = 0x0098A9C2;

const TInt KMenuTop = 72;

_LIT(KLanguageFolder, "C:\\Data\\N-GBA\\");
_LIT(KLanguageFile, "C:\\Data\\N-GBA\\language.dat");

// --------------------------------------------------
// Penyimpanan bahasa
// --------------------------------------------------

TInt LoadLanguageMode()
{
    RFs fs;

    if (fs.Connect() != KErrNone) {
        return ELanguageAuto;
    }

    RFile file;

    TInt error = file.Open(
        fs,
        KLanguageFile,
        EFileRead | EFileShareReadersOnly
    );

    TInt mode = ELanguageAuto;

    if (error == KErrNone) {
        TBuf8<1> data;

        error = file.Read(data);

        if (error == KErrNone && data.Length() > 0) {
            const TInt value = data[0];

            if (
                value >= ELanguageAuto &&
                value <= ELanguageEnglish
            ) {
                mode = value;
            }
        }

        file.Close();
    }

    fs.Close();

    return mode;
}

TInt SaveLanguageMode(TInt mode)
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
        KLanguageFile,
        EFileWrite | EFileShareExclusive
    );

    if (error == KErrNone) {
        TBuf8<1> data;

        data.Append(static_cast<TUint8>(mode));

        error = file.Write(data);

        file.Close();
    }

    fs.Close();

    return error;
}

// --------------------------------------------------
// Deteksi bahasa
// --------------------------------------------------

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
// Menu
// --------------------------------------------------

TInt MenuCount(TInt page)
{
    if (page == ESettingsPage) {
        return KSettingsMenuCount;
    }

    if (page == ELanguagePage) {
        return KLanguageMenuCount;
    }

    return KMainMenuCount;
}

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
                "KEMBALI"
            };

            return menu[index];
        }

        static const char* menu[] = {
            "LANGUAGE",
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

    return "";
}

// --------------------------------------------------
// Grafik
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

    while (
        text[length] != '\0' &&
        length < 96
    ) {
        buffer[length] =
            static_cast<TUint8>(text[length]);

        ++length;
    }

    gc.SetPenStyle(CGraphicsContext::ESolidPen);
    gc.SetPenColor(TRgb(color));

    gc.DrawText(
        TPtrC16(buffer, length),
        TPoint(x, y)
    );
}

void DrawMenu(
    CWindowGc& gc,
    CFont* font,
    const TSize& size,
    TInt page,
    TInt selected,
    TInt languageMode,
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
        size.iHeight >= 300 ? 27 : 22;

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

        DrawText(
            gc,
            MenuText(page, i, indonesian),
            15,
            y + 15,
            (i == selected) ? KWhite : KMuted
        );

        if (
            page == ELanguagePage &&
            i == languageMode
        ) {
            DrawText(
                gc,
                "*",
                size.iWidth - 18,
                y + 15,
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
// Aktivasi menu
// --------------------------------------------------

void ActivateSelected(
    TInt& page,
    TInt& selected,
    TInt& languageMode,
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
    }
}

// --------------------------------------------------
// Kembali
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

    } else {

        page = EMainPage;
        selected = 4;
    }
}

// --------------------------------------------------
// Window Server
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

                CFont* font = NULL;

                _LIT(KTypeface, "");

                const TFontSpec fontSpec(
                    KTypeface,
                    120
                );

                error =
                    screen.GetNearestFontToDesignHeightInTwips(
                        font,
                        fontSpec
                    );

                if (error == KErrNone) {

                    RWindowGroup group(session);

                    error = group.Construct(1, ETrue);

                    if (error == KErrNone) {

                        RWindow window(session);

                        error = window.Construct(group, 2);

                        if (error == KErrNone) {

                            TInt languageMode =
                                LoadLanguageMode();

                            TBool indonesian =
                                IsIndonesian(languageMode);

                            TInt page = EMainPage;
                            TInt selected = 0;

                            // Baris terakhir yang disentuh
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

                            while (running) {

                                User::WaitForAnyRequest();

                                // --------------------------
                                // Input keyboard dan sentuhan
                                // --------------------------

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

                                        // TOUCHSCREEN
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
                                                    size.iHeight >= 300
                                                    ? 27 : 22;

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

                                            // TOMBOL FISIK
                                            key = event.Key()->iCode;
                                            lastTappedRow = -1;
                                        }

                                        // NAVIGASI ATAS
                                        if (key == EKeyUpArrow) {

                                            selected =
                                                (selected +
                                                MenuCount(page) - 1) %
                                                MenuCount(page);

                                            window.Invalidate();

                                        // NAVIGASI BAWAH
                                        } else if (
                                            key == EKeyDownArrow
                                        ) {

                                            selected =
                                                (selected + 1) %
                                                MenuCount(page);

                                            window.Invalidate();

                                        // PILIH MENU
                                        } else if (
                                            key == EKeyEnter ||
                                            key == EKeyDevice3
                                        ) {

                                            ActivateSelected(
                                                page,
                                                selected,
                                                languageMode,
                                                indonesian,
                                                running,
                                                message
                                            );

                                            window.Invalidate();

                                        // KEMBALI
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
                                // Redraw
                                // --------------------------

                                if (redraws != KRequestPending) {

                                    if (redraws.Int() != KErrNone) {
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
                            // Cleanup
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
// Application entry
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


#include "window_server.h"
#include "rom_browser.h"

#include <e32base.h>
#include <e32keys.h>
#include <w32std.h>

namespace {

const TInt KMenuCount = 6;
const TInt KMaxRoms = 8;

const TUint KBackground = 0x00080D18;
const TUint KPanel      = 0x00121D30;
const TUint KBlue       = 0x00308CFF;
const TUint KWhite      = 0x00FFFFFF;
const TUint KMuted      = 0x0098A9C2;

const char* const KMenu[KMenuCount] = {
    "GAME LIBRARY",
    "LOAD ROM",
    "RESUME GAME",
    "SAVE STATES",
    "SETTINGS",
    "ABOUT N-GBA"
};

void Fill(
    CWindowGc& gc,
    TInt x,
    TInt y,
    TInt width,
    TInt height,
    TUint color
) {
    gc.SetPenStyle(CGraphicsContext::ENullPen);
    gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
    gc.SetBrushColor(TRgb(color));
    gc.DrawRect(TRect(x, y, x + width, y + height));
}

void DrawText(
    CWindowGc& gc,
    const char* text,
    TInt x,
    TInt y,
    TUint color
) {
    TUint16 buffer[96];
    TInt length = 0;

    while (text[length] != '\0' && length < 96) {
        buffer[length] = static_cast<TUint8>(text[length]);
        ++length;
    }

    gc.SetPenStyle(CGraphicsContext::ESolidPen);
    gc.SetPenColor(TRgb(color));
    gc.DrawText(TPtrC16(buffer, length), TPoint(x, y));
}

void DrawDescriptor(
    CWindowGc& gc,
    const TDesC& text,
    TInt x,
    TInt y,
    TUint color
) {
    gc.SetPenStyle(CGraphicsContext::ESolidPen);
    gc.SetPenColor(TRgb(color));
    gc.DrawText(text, TPoint(x, y));
}

void DrawMenu(
    CWindowGc& gc,
    CFont* font,
    const TSize& size,
    TInt selected,
    const char* message
) {
    Fill(gc, 0, 0, size.iWidth, size.iHeight, KBackground);
    Fill(gc, 0, 0, size.iWidth, 4, KBlue);

    gc.UseFont(font);

    DrawText(gc, "N-GBA", 12, 30, KWhite);
    DrawText(gc, "STELLAR DRAGON", 12, 51, KBlue);

    const TInt top = 72;
    const TInt rowHeight = size.iHeight >= 300 ? 31 : 25;

    for (TInt i = 0; i < KMenuCount; ++i) {
        const TInt y = top + i * rowHeight;

        if (i == selected) {
            Fill(gc, 8, y, size.iWidth - 16,
                 rowHeight - 3, KBlue);
            DrawText(gc, KMenu[i], 18, y + 16, KWhite);
        } else {
            Fill(gc, 8, y, size.iWidth - 16,
                 rowHeight - 3, KPanel);
            DrawText(gc, KMenu[i], 18, y + 16, KMuted);
        }
    }

    DrawText(gc, message, 8, size.iHeight - 30, KBlue);
    gc.DiscardFont();
}

void DrawRomBrowser(
    CWindowGc& gc,
    CFont* font,
    const TSize& size,
    TFileName* files,
    TInt count,
    TInt selected,
    const char* message
) {
    Fill(gc, 0, 0, size.iWidth, size.iHeight, KBackground);
    Fill(gc, 0, 0, size.iWidth, 4, KBlue);

    gc.UseFont(font);

    DrawText(gc, "N-GBA / ROM LIST", 10, 28, KWhite);

    if (count <= 0) {
        DrawText(gc, "NO GBA ROM FOUND", 12, 75, KMuted);
        DrawText(gc, "COPY ROM TO C:\\GBA", 12, 100, KMuted);
        DrawText(gc, "OR E:\\GBA", 12, 122, KMuted);
    } else {
        const TInt rowHeight = size.iHeight >= 300 ? 25 : 21;
        const TInt top = 48;

        for (TInt i = 0; i < count; ++i) {
            const TInt y = top + i * rowHeight;

            if (i == selected) {
                Fill(gc, 6, y, size.iWidth - 12,
                     rowHeight - 2, KBlue);
            } else {
                Fill(gc, 6, y, size.iWidth - 12,
                     rowHeight - 2, KPanel);
            }

            // Display only the filename, not the full drive path.
            TInt start = 0;
            for (TInt j = 0; j < files[i].Length(); ++j) {
                if (files[i][j] == '\\' || files[i][j] == '/') {
                    start = j + 1;
                }
            }

            TPtrC name(files[i].Mid(start));

            DrawDescriptor(
                gc,
                name,
                12,
                y + rowHeight - 7,
                i == selected ? KWhite : KMuted
            );
        }
    }

    DrawText(gc, message, 8, size.iHeight - 30, KBlue);
    gc.DiscardFont();
}

TInt RunWindowServer() {
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

                CFont* font = NULL;
                _LIT(KTypeface, "");
                const TFontSpec fontSpec(KTypeface, 120);

                error = screen.GetNearestFontToDesignHeightInTwips(
                    font, fontSpec
                );

                if (error == KErrNone) {
                    RWindowGroup group(session);
                    error = group.Construct(1, ETrue);

                    if (error == KErrNone) {
                        RWindow window(session);
                        error = window.Construct(group, 2);

                        if (error == KErrNone) {
                            TInt selected = 0;
                            TBool running = ETrue;
                            TBool showingRoms = EFalse;

                            TFileName romFiles[KMaxRoms];
                            TInt romCount = 0;
                            TInt romSelected = 0;

                            const char* message =
                                "UP DOWN SELECT BACK";
                            const char* browserMessage =
                                "UP DOWN SELECT BACK";

                            group.SetOrdinalPosition(0);
                            window.SetExtent(TPoint(0, 0), size);
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

                                if (events != KRequestPending) {
                                    if (events.Int() != KErrNone) {
                                        error = events.Int();
                                        break;
                                    }

                                    TWsEvent event;
                                    session.GetEvent(event);

                                    if (
                                        event.Type() == EEventKey ||
                                        event.Type() == EEventKeyDown
                                    ) {
                                        const TUint key =
                                            event.Key()->iCode;

                                        if (key == EKeyUpArrow) {
                                            if (showingRoms) {
                                                if (romCount > 0) {
                                                    romSelected =
                                                        (romSelected +
                                                         romCount - 1) %
                                                        romCount;
                                                }
                                            } else {
                                                selected =
                                                    (selected +
                                                     KMenuCount - 1) %
                                                    KMenuCount;
                                            }
                                            window.Invalidate();

                                        } else if (key == EKeyDownArrow) {
                                            if (showingRoms) {
                                                if (romCount > 0) {
                                                    romSelected =
                                                        (romSelected + 1) %
                                                        romCount;
                                                }
                                            } else {
                                                selected =
                                                    (selected + 1) %
                                                    KMenuCount;
                                            }
                                            window.Invalidate();

                                        } else if (
                                            key == EKeyEnter ||
                                            key == EKeyDevice3
                                        ) {
                                            if (showingRoms) {
                                                if (romCount > 0) {
                                                    browserMessage =
                                                        "ROM LOADER NOT READY";
                                                }
                                            } else {
                                                switch (selected) {
                                                    case 0:
                                                    case 1:
                                                        romCount = ScanRoms(
                                                            romFiles,
                                                            KMaxRoms
                                                        );
                                                        romSelected = 0;
                                                        showingRoms = ETrue;

                                                        if (romCount < 0) {
                                                            romCount = 0;
                                                            browserMessage =
                                                                "ROM SCAN ERROR";
                                                        } else if (romCount == 0) {
                                                            browserMessage =
                                                                "NO GBA ROM FOUND";
                                                        } else {
                                                            browserMessage =
                                                                "SELECT ROM";
                                                        }
                                                        break;

                                                    case 2:
                                                        message =
                                                            "NO SAVED GAME YET";
                                                        break;

                                                    case 3:
                                                        message =
                                                            "SAVE STATES SELECTED";
                                                        break;

                                                    case 4:
                                                        message =
                                                            "SETTINGS SELECTED";
                                                        break;

                                                    case 5:
                                                        message =
                                                            "N-GBA GBA EMULATOR";
                                                        break;
                                                }
                                            }
                                            window.Invalidate();

                                        } else if (
                                            key == EKeyBackspace ||
                                            key == EKeyEscape
                                        ) {
                                            if (showingRoms) {
                                                showingRoms = EFalse;
                                                message =
                                                    "UP DOWN SELECT BACK";
                                            } else {
                                                running = EFalse;
                                            }
                                            window.Invalidate();
                                        }
                                    }

                                    if (running) {
                                        session.EventReady(&events);
                                    }
                                }

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

                                        if (showingRoms) {
                                            DrawRomBrowser(
                                                gc,
                                                font,
                                                size,
                                                romFiles,
                                                romCount,
                                                romSelected,
                                                browserMessage
                                            );
                                        } else {
                                            DrawMenu(
                                                gc,
                                                font,
                                                size,
                                                selected,
                                                message
                                            );
                                        }

                                        gc.Deactivate();
                                        window.EndRedraw();
                                    }

                                    if (running) {
                                        session.RedrawReady(&redraws);
                                    }
                                }

                                session.Flush();
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
            }
        }
    }

    session.Close();
    return error;
}

} // namespace

int GuiWindowServerMain() {
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

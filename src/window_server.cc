
#include "window_server.h"

#include <e32base.h>
#include <e32keys.h>
#include <w32std.h>

namespace {

const TInt KMenuCount = 7;

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
    "ABOUT N-GBA",
    "EXIT / KELUAR"
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

    gc.DrawText(
        TPtrC16(buffer, length),
        TPoint(x, y)
    );
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
    const TInt rowHeight =
        size.iHeight >= 300 ? 27 : 22;

    for (TInt i = 0; i < KMenuCount; ++i) {
        const TInt y = top + i * rowHeight;

        if (i == selected) {
            Fill(
                gc,
                8,
                y,
                size.iWidth - 16,
                rowHeight - 2,
                KBlue
            );

            DrawText(gc, KMenu[i], 18, y + 15, KWhite);
        } else {
            Fill(
                gc,
                8,
                y,
                size.iWidth - 16,
                rowHeight - 2,
                KPanel
            );

            DrawText(gc, KMenu[i], 18, y + 15, KMuted);
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
                            TInt selected = 0;
                            TBool running = ETrue;

                            const char* message =
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
                                            selected =
                                                (selected +
                                                KMenuCount - 1) %
                                                KMenuCount;

                                            window.Invalidate();

                                        } else if (
                                            key == EKeyDownArrow
                                        ) {
                                            selected =
                                                (selected + 1) %
                                                KMenuCount;

                                            window.Invalidate();

                                        } else if (
                                            key == EKeyEnter ||
                                            key == EKeyDevice3
                                        ) {
                                            switch (selected) {
                                                case 0:
                                                    message =
                                                        "GAME LIBRARY SELECTED";
                                                    break;

                                                case 1:
                                                    message =
                                                        "LOAD ROM SELECTED";
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

                                                case 6:
                                                    running = EFalse;
                                                    break;
                                            }

                                            window.Invalidate();

                                        } else if (
                                            key == EKeyBackspace ||
                                            key == EKeyEscape
                                        ) {
                                            running = EFalse;
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

                                        DrawMenu(
                                            gc,
                                            font,
                                            size,
                                            selected,
                                            message
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

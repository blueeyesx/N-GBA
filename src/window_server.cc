
#include "window_server.h"

#include <e32base.h>
#include <e32keys.h>
#include <f32file.h>
#include <w32std.h>

namespace {

const TInt KMenuCount = 6;

const TUint KBackground = 0x00080D18;
const TUint KPanel      = 0x00121D30;
const TUint KBlue       = 0x00308CFF;
const TUint KWhite      = 0x00FFFFFF;
const TUint KMuted      = 0x0098A9C2;
const TUint KGreen      = 0x0045D483;
const TUint KRed        = 0x00FF7777;

const char* const KMenu[KMenuCount] = {
    "GAME LIBRARY",
    "LOAD ROM",
    "RESUME GAME",
    "SAVE STATES",
    "SETTINGS",
    "ABOUT N-GBA"
};

TFileName gCurrentPath;
TFileName gSelectedRom;

CDir* gFiles = NULL;
CDir* gDirectories = NULL;

TInt gBrowserSelection = 0;
TInt gBrowserScroll = 0;

TBool gBrowserActive = EFalse;
TBool gRomSelected = EFalse;

const char* gBrowserMessage = "SELECT A GBA ROM";

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

    while (text[length] != '\0' && length < 95) {
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
    TUint color,
    TInt maximumLength
) {
    const TInt length =
        text.Length() < maximumLength
        ? text.Length()
        : maximumLength;

    gc.SetPenStyle(CGraphicsContext::ESolidPen);
    gc.SetPenColor(TRgb(color));

    gc.DrawText(
        text.Left(length),
        TPoint(x, y)
    );
}

void CloseDirectoryLists() {
    delete gFiles;
    delete gDirectories;

    gFiles = NULL;
    gDirectories = NULL;
}

TBool IsGbaFile(const TEntry& entry) {
    if (entry.iAtt & KEntryAttDir) {
        return EFalse;
    }

    if (entry.iName.Length() < 4) {
        return EFalse;
    }

    return entry.iName.Right(4).CompareF(_L(".gba")) == 0;
}

TInt VisibleEntryCount() {
    TInt count = 0;

    if (gDirectories != NULL) {
        count += gDirectories->Count();
    }

    if (gFiles != NULL) {
        for (TInt i = 0; i < gFiles->Count(); ++i) {
            if (IsGbaFile((*gFiles)[i])) {
                ++count;
            }
        }
    }

    return count;
}

TInt LoadDirectory(RFs& fs) {
    CloseDirectoryLists();

    TFileName searchPath;
    searchPath.Copy(gCurrentPath);

    if (searchPath.Length() == 0) {
        return KErrArgument;
    }

    if (searchPath[searchPath.Length() - 1] != '\\') {
        searchPath.Append(_L("\\"));
    }

    searchPath.Append(_L("*"));

    const TInt error = fs.GetDir(
        searchPath,
        KEntryAttNormal,
        ESortByName,
        gFiles,
        gDirectories
    );

    if (error != KErrNone) {
        CloseDirectoryLists();
        return error;
    }

    gBrowserSelection = 0;
    gBrowserScroll = 0;

    return KErrNone;
}

TInt EnterBrowser(RFs& fs, TChar drive) {
    CloseDirectoryLists();

    gCurrentPath.Zero();
    gCurrentPath.Append(drive);
    gCurrentPath.Append(_L(":\\"));

    gRomSelected = EFalse;
    gSelectedRom.Zero();

    const TInt error = LoadDirectory(fs);

    if (error == KErrNone) {
        gBrowserActive = ETrue;
        gBrowserMessage = "SELECT A GBA ROM";
    }

    return error;
}

TInt EnterFolder(RFs& fs, const TEntry& entry) {
    if (!(entry.iAtt & KEntryAttDir)) {
        return KErrArgument;
    }

    if (gCurrentPath.Length() + entry.iName.Length() + 1
        >= KMaxFileName) {
        return KErrTooBig;
    }

    gCurrentPath.Append(entry.iName);
    gCurrentPath.Append(_L("\\"));

    const TInt error = LoadDirectory(fs);

    if (error != KErrNone) {
        return error;
    }

    gRomSelected = EFalse;
    gSelectedRom.Zero();

    return KErrNone;
}

TInt GoParentFolder(RFs& fs) {
    const TInt length = gCurrentPath.Length();

    if (length <= 3) {
        gBrowserActive = EFalse;
        CloseDirectoryLists();
        return KErrNone;
    }

    TInt end = length;

    if (gCurrentPath[end - 1] == '\\') {
        --end;
    }

    TInt separator = end - 1;

    while (separator >= 0 &&
           gCurrentPath[separator] != '\\') {
        --separator;
    }

    if (separator < 2) {
        gCurrentPath.SetLength(3);
    } else {
        gCurrentPath.SetLength(separator + 1);
    }

    const TInt error = LoadDirectory(fs);

    if (error == KErrNone) {
        gRomSelected = EFalse;
        gSelectedRom.Zero();
    }

    return error;
}

TInt SelectBrowserEntry(RFs& fs) {
    TInt index = gBrowserSelection;

    if (gDirectories != NULL) {
        const TInt directoryCount = gDirectories->Count();

        if (index < directoryCount) {
            return EnterFolder(fs, (*gDirectories)[index]);
        }

        index -= directoryCount;
    }

    if (gFiles != NULL) {
        for (TInt i = 0; i < gFiles->Count(); ++i) {
            const TEntry& entry = (*gFiles)[i];

            if (!IsGbaFile(entry)) {
                continue;
            }

            if (index == 0) {
                gSelectedRom.Copy(gCurrentPath);
                gSelectedRom.Append(entry.iName);

                gRomSelected = ETrue;
                gBrowserMessage = "ROM SELECTED";

                return KErrNone;
            }

            --index;
        }
    }

    return KErrNotFound;
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
        size.iHeight >= 300 ? 31 : 25;

    for (TInt i = 0; i < KMenuCount; ++i) {
        const TInt y = top + i * rowHeight;

        if (i == selected) {
            Fill(
                gc, 8, y,
                size.iWidth - 16,
                rowHeight - 3,
                KBlue
            );

            DrawText(
                gc, KMenu[i],
                18, y + 16,
                KWhite
            );
        } else {
            Fill(
                gc, 8, y,
                size.iWidth - 16,
                rowHeight - 3,
                KPanel
            );

            DrawText(
                gc, KMenu[i],
                18, y + 16,
                KMuted
            );
        }
    }

    DrawText(
        gc,
        message,
        8,
        size.iHeight - 30,
        KBlue
    );

    gc.DiscardFont();
}

void DrawBrowser(
    CWindowGc& gc,
    CFont* font,
    const TSize& size
) {
    Fill(gc, 0, 0, size.iWidth, size.iHeight, KBackground);
    Fill(gc, 0, 0, size.iWidth, 4, KBlue);

    gc.UseFont(font);

    DrawText(gc, "N-GBA / ROM BROWSER", 8, 22, KWhite);

    DrawDescriptor(
        gc,
        gCurrentPath,
        8,
        43,
        KBlue,
        36
    );

    Fill(
        gc,
        6, 52,
        size.iWidth - 12,
        size.iHeight - 91,
        KPanel
    );

    const TInt rowHeight =
        size.iHeight >= 300 ? 25 : 21;

    const TInt firstY = 72;

    const TInt visibleRows =
        (size.iHeight - 104) / rowHeight;

    const TInt total = VisibleEntryCount();

    if (gBrowserSelection < 0) {
        gBrowserSelection = 0;
    }

    if (gBrowserSelection >= total && total > 0) {
        gBrowserSelection = total - 1;
    }

    if (gBrowserSelection < gBrowserScroll) {
        gBrowserScroll = gBrowserSelection;
    }

    if (gBrowserSelection >= gBrowserScroll + visibleRows) {
        gBrowserScroll =
            gBrowserSelection - visibleRows + 1;
    }

    TInt visibleIndex = 0;
    TInt row = 0;

    if (gDirectories != NULL) {
        for (TInt i = 0;
             i < gDirectories->Count();
             ++i) {

            if (visibleIndex >= gBrowserScroll &&
                row < visibleRows) {

                const TInt y = firstY + row * rowHeight;

                if (visibleIndex == gBrowserSelection) {
                    Fill(
                        gc,
                        10, y - 15,
                        size.iWidth - 20,
                        rowHeight,
                        KBlue
                    );
                }

                DrawText(
                    gc,
                    "[DIR]",
                    14,
                    y,
                    KGreen
                );

                DrawDescriptor(
                    gc,
                    (*gDirectories)[i].iName,
                    54,
                    y,
                    visibleIndex == gBrowserSelection
                        ? KWhite : KMuted,
                    27
                );

                ++row;
            }

            ++visibleIndex;
        }
    }

    if (gFiles != NULL) {
        for (TInt i = 0; i < gFiles->Count(); ++i) {
            const TEntry& entry = (*gFiles)[i];

            if (!IsGbaFile(entry)) {
                continue;
            }

            if (visibleIndex >= gBrowserScroll &&
                row < visibleRows) {

                const TInt y = firstY + row * rowHeight;

                if (visibleIndex == gBrowserSelection) {
                    Fill(
                        gc,
                        10, y - 15,
                        size.iWidth - 20,
                        rowHeight,
                        KBlue
                    );
                }

                DrawText(
                    gc,
                    "[GBA]",
                    14,
                    y,
                    KGreen
                );

                DrawDescriptor(
                    gc,
                    entry.iName,
                    54,
                    y,
                    visibleIndex == gBrowserSelection
                        ? KWhite : KMuted,
                    27
                );

                ++row;
            }

            ++visibleIndex;
        }
    }

    if (total == 0) {
        DrawText(
            gc,
            "NO GBA FILES",
            18,
            firstY + 15,
            KMuted
        );
    }

    const TInt footerY = size.iHeight - 30;

    DrawText(
        gc,
        gBrowserMessage,
        8,
        footerY,
        gRomSelected ? KGreen : KBlue
    );

    if (gRomSelected) {
        DrawDescriptor(
            gc,
            gSelectedRom,
            8,
            size.iHeight - 12,
            KGreen,
            42
        );
    }

    gc.DiscardFont();
}

TInt RunWindowServer() {
    RFs fs;

    TInt error = fs.Connect();

    if (error != KErrNone) {
        return error;
    }

    RWsSession session;

    error = session.Connect();

    if (error != KErrNone) {
        fs.Close();
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

                                        if (gBrowserActive) {
                                            if (key == EKeyUpArrow) {
                                                if (gBrowserSelection > 0) {
                                                    --gBrowserSelection;
                                                }

                                                window.Invalidate();
                                            }
                                            else if (key == EKeyDownArrow) {
                                                const TInt count =
                                                    VisibleEntryCount();

                                                if (gBrowserSelection + 1 < count) {
                                                    ++gBrowserSelection;
                                                }

                                                window.Invalidate();
                                            }
                                            else if (
                                                key == EKeyEnter ||
                                                key == EKeyDevice3
                                            ) {
                                                const TInt result =
                                                    SelectBrowserEntry(fs);

                                                if (result == KErrAccessDenied) {
                                                    gBrowserMessage =
                                                        "ACCESS DENIED";
                                                }
                                                else if (result == KErrNotFound) {
                                                    gBrowserMessage =
                                                        "NO ENTRY";
                                                }
                                                else if (result != KErrNone) {
                                                    gBrowserMessage =
                                                        "CANNOT OPEN FOLDER";
                                                }

                                                window.Invalidate();
                                            }
                                            else if (
                                                key == EKeyBackspace ||
                                                key == EKeyEscape
                                            ) {
                                                const TInt result =
                                                    GoParentFolder(fs);

                                                if (result != KErrNone) {
                                                    gBrowserMessage =
                                                        "CANNOT OPEN FOLDER";
                                                }

                                                window.Invalidate();
                                            }
                                            else if (
                                                key == EKeyLeftArrow ||
                                                key == EKeyRightArrow
                                            ) {
                                                if (gCurrentPath.Length() == 3) {
                                                    const TChar drive =
                                                        key == EKeyLeftArrow
                                                        ? 'C' : 'E';

                                                    const TInt result =
                                                        EnterBrowser(fs, drive);

                                                    if (result != KErrNone) {
                                                        gBrowserMessage =
                                                            "DRIVE NOT AVAILABLE";
                                                    }

                                                    window.Invalidate();
                                                }
                                            }
                                        }
                                        else {
                                            if (key == EKeyUpArrow) {
                                                selected =
                                                    (selected +
                                                     KMenuCount - 1) %
                                                    KMenuCount;

                                                window.Invalidate();
                                            }
                                            else if (key == EKeyDownArrow) {
                                                selected =
                                                    (selected + 1) %
                                                    KMenuCount;

                                                window.Invalidate();
                                            }
                                            else if (
                                                key == EKeyEnter ||
                                                key == EKeyDevice3
                                            ) {
                                                switch (selected) {
                                                    case 0:
                                                    case 1: {
                                                        const TInt result =
                                                            EnterBrowser(fs, 'C');

                                                        if (result != KErrNone) {
                                                            message =
                                                                "C DRIVE ERROR";
                                                        }
                                                        else {
                                                            message =
                                                                "ROM BROWSER";
                                                        }

                                                        break;
                                                    }

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

                                                window.Invalidate();
                                            }
                                            else if (
                                                key == EKeyBackspace ||
                                                key == EKeyEscape
                                            ) {
                                                running = EFalse;
                                            }
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

                                        if (gBrowserActive) {
                                            DrawBrowser(gc, font, size);
                                        }
                                        else {
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

    CloseDirectoryLists();

    session.Close();
    fs.Close();

    return error;
}

} // namespace

int GuiWindowServerMain() {
    return RunWindowServer();
}

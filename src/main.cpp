
#include <e32base.h>
#include <e32keys.h>
#include <w32std.h>

namespace {

const TInt KMenuCount = 6;

const TUint KSelectedBlue = 0x00308cff;
const TUint KBackground  = 0x00080d18;
const TUint KPanel       = 0x00121d30;
const TUint KWhite       = 0x00ffffff;
const TUint KMuted       = 0x0098a9c2;

const char* const KMenu[KMenuCount] = {
    "GAME LIBRARY",
    "LOAD ROM",
    "RESUME GAME",
    "SAVE STATES",
    "SETTINGS",
    "ABOUT N-GBA"
};

// ---------------------------------------------------------
// 5x7 bitmap font
// ---------------------------------------------------------

void GlyphRows(char c, TUint8 rows[7]) {
    const TUint8 blank[7] = {
        0, 0, 0, 0, 0, 0, 0
    };

    const TUint8* p = blank;

    static const TUint8 A[7] = {14,17,17,31,17,17,17};
    static const TUint8 B[7] = {30,17,17,30,17,17,30};
    static const TUint8 C[7] = {14,17,16,16,16,17,14};
    static const TUint8 D[7] = {30,17,17,17,17,17,30};
    static const TUint8 E[7] = {31,16,16,30,16,16,31};
    static const TUint8 F[7] = {31,16,16,30,16,16,16};
    static const TUint8 G[7] = {14,17,16,23,17,17,15};
    static const TUint8 H[7] = {17,17,17,31,17,17,17};
    static const TUint8 I[7] = {14,4,4,4,4,4,14};
    static const TUint8 J[7] = {7,2,2,2,2,18,12};
    static const TUint8 K[7] = {17,18,20,24,20,18,17};
    static const TUint8 L[7] = {16,16,16,16,16,16,31};
    static const TUint8 M[7] = {17,27,21,21,17,17,17};
    static const TUint8 N[7] = {17,25,21,19,17,17,17};
    static const TUint8 O[7] = {14,17,17,17,17,17,14};
    static const TUint8 P[7] = {30,17,17,30,16,16,16};
    static const TUint8 Q[7] = {14,17,17,17,21,18,13};
    static const TUint8 R[7] = {30,17,17,30,20,18,17};
    static const TUint8 S[7] = {15,16,16,14,1,1,30};
    static const TUint8 T[7] = {31,4,4,4,4,4,4};
    static const TUint8 U[7] = {17,17,17,17,17,17,14};
    static const TUint8 V[7] = {17,17,17,17,17,10,4};
    static const TUint8 W[7] = {17,17,17,21,21,21,10};
    static const TUint8 X[7] = {17,17,10,4,10,17,17};
    static const TUint8 Y[7] = {17,17,10,4,4,4,4};
    static const TUint8 Z[7] = {31,1,2,4,8,16,31};

    static const TUint8 N0[7] = {14,17,19,21,25,17,14};
    static const TUint8 N1[7] = {4,12,4,4,4,4,14};
    static const TUint8 N2[7] = {14,17,1,2,4,8,31};
    static const TUint8 N3[7] = {30,1,1,14,1,1,30};
    static const TUint8 N4[7] = {2,6,10,18,31,2,2};
    static const TUint8 N5[7] = {31,16,16,30,1,1,30};
    static const TUint8 N6[7] = {14,16,16,30,17,17,14};
    static const TUint8 N7[7] = {31,1,2,4,8,8,8};
    static const TUint8 N8[7] = {14,17,17,14,17,17,14};
    static const TUint8 N9[7] = {14,17,17,15,1,1,14};

    static const TUint8 DASH[7]  = {0,0,0,31,0,0,0};
    static const TUint8 COLON[7] = {0,4,4,0,4,4,0};

    switch (c) {
        case 'A': p=A; break;
        case 'B': p=B; break;
        case 'C': p=C; break;
        case 'D': p=D; break;
        case 'E': p=E; break;
        case 'F': p=F; break;
        case 'G': p=G; break;
        case 'H': p=H; break;
        case 'I': p=I; break;
        case 'J': p=J; break;
        case 'K': p=K; break;
        case 'L': p=L; break;
        case 'M': p=M; break;
        case 'N': p=N; break;
        case 'O': p=O; break;
        case 'P': p=P; break;
        case 'Q': p=Q; break;
        case 'R': p=R; break;
        case 'S': p=S; break;
        case 'T': p=T; break;
        case 'U': p=U; break;
        case 'V': p=V; break;
        case 'W': p=W; break;
        case 'X': p=X; break;
        case 'Y': p=Y; break;
        case 'Z': p=Z; break;

        case '0': p=N0; break;
        case '1': p=N1; break;
        case '2': p=N2; break;
        case '3': p=N3; break;
        case '4': p=N4; break;
        case '5': p=N5; break;
        case '6': p=N6; break;
        case '7': p=N7; break;
        case '8': p=N8; break;
        case '9': p=N9; break;

        case '-': p=DASH; break;
        case ':': p=COLON; break;

        default:
            p=blank;
            break;
    }

    for (TInt i=0; i<7; ++i) {
        rows[i]=p[i];
    }
}

// ---------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------

void Fill(
    CWindowGc& gc,
    TInt x,
    TInt y,
    TInt w,
    TInt h,
    TUint color
) {
    gc.SetBrushColor(TRgb(color));
    gc.SetPenStyle(CGraphicsContext::ENullPen);
    gc.SetBrushStyle(CGraphicsContext::ESolidBrush);

    gc.DrawRect(TRect(x,y,x+w,y+h));
}

void DrawText5x7(
    CWindowGc& gc,
    const char* text,
    TInt x,
    TInt y,
    TInt scale,
    TUint color
) {
    gc.SetBrushColor(TRgb(color));
    gc.SetPenStyle(CGraphicsContext::ENullPen);
    gc.SetBrushStyle(CGraphicsContext::ESolidBrush);

    TInt cursor=x;

    while (*text) {
        TUint8 rows[7];

        GlyphRows(*text++,rows);

        for (TInt row=0; row<7; ++row) {
            for (TInt col=0; col<5; ++col) {
                if (rows[row] & (1 << (4-col))) {
                    gc.DrawRect(
                        TRect(
                            cursor+col*scale,
                            y+row*scale,
                            cursor+(col+1)*scale,
                            y+(row+1)*scale
                        )
                    );
                }
            }
        }

        cursor += 6*scale;
    }
}

// ---------------------------------------------------------
// Main menu drawing
// ---------------------------------------------------------

void DrawGui(
    CWindowGc& gc,
    const TSize& size,
    TInt selected
) {
    Fill(
        gc,
        0,
        0,
        size.iWidth,
        size.iHeight,
        KBackground
    );

    // Blue top line
    Fill(
        gc,
        0,
        0,
        size.iWidth,
        4,
        KSelectedBlue
    );

    const TInt scale =
        (size.iWidth >= 300) ? 2 : 1;

    // Application title
    DrawText5x7(
        gc,
        "N-GBA",
        12,
        14,
        scale,
        KWhite
    );

    DrawText5x7(
        gc,
        "STELLAR DRAGON",
        12,
        14+10*scale,
        1,
        KSelectedBlue
    );

    const TInt top=56;

    const TInt rowH =
        (size.iHeight >= 300) ? 31 : 25;

    for (TInt i=0; i<KMenuCount; ++i) {
        const TInt y=top+i*rowH;

        if (i==selected) {
            Fill(
                gc,
                8,
                y,
                size.iWidth-16,
                rowH-3,
                KSelectedBlue
            );

            DrawText5x7(
                gc,
                KMenu[i],
                18,
                y+((rowH-7)/2)-2,
                1,
                KWhite
            );
        }
        else {
            Fill(
                gc,
                8,
                y,
                size.iWidth-16,
                rowH-3,
                KPanel
            );

            DrawText5x7(
                gc,
                KMenu[i],
                18,
                y+((rowH-7)/2)-2,
                1,
                KMuted
            );
        }
    }

    // Footer
    const TInt footerY=size.iHeight-18;

    DrawText5x7(
        gc,
        "UP DOWN SELECT BACK",
        8,
        footerY,
        1,
        KMuted
    );
}

// ---------------------------------------------------------
// Native Symbian Window Server
// ---------------------------------------------------------

TInt RunGui() {
    RWsSession session;

    TInt err=session.Connect();

    if (err!=KErrNone) {
        return err;
    }

    {
        // CWsScreenDevice takes RWsSession reference.
        CWsScreenDevice screen(session);

        err=screen.Construct();

        if (err==KErrNone) {
            // CWindowGc is destroyed automatically.
            CWindowGc gc(&screen);

            err=gc.Construct();

            if (err==KErrNone) {
                RWindowGroup group(session);

                err=group.Construct(1,ETrue);

                if (err==KErrNone) {
                    RWindow window(session);

                    err=window.Construct(group,2);

                    if (err==KErrNone) {
                        const TSize size=screen.SizeInPixels();

                        group.SetOrdinalPosition(0);

                        window.SetExtent(
                            TPoint(0,0),
                            size
                        );

                        window.SetVisible(ETrue);
                        window.Activate();

                        TInt selected=0;
                        TBool running=ETrue;

                        window.Invalidate();
                        session.Flush();

                        TRequestStatus events;
                        TRequestStatus redraws;

                        session.EventReady(&events);
                        session.RedrawReady(&redraws);

                        while (running) {
                            User::WaitForAnyRequest();

                            // Keyboard events
                            if (events!=KRequestPending) {
                                if (events.Int()!=KErrNone) {
                                    err=events.Int();
                                    break;
                                }

                                TWsEvent event;

                                session.GetEvent(event);

                                if (
                                    event.Type()==EEventKey ||
                                    event.Type()==EEventKeyDown
                                ) {
                                    const TUint key =
                                        event.Key()->iCode;

                                    if (key==EKeyUpArrow) {
                                        selected=
                                            (selected+KMenuCount-1)
                                            %KMenuCount;

                                        window.Invalidate();
                                    }
                                    else if (key==EKeyDownArrow) {
                                        selected=
                                            (selected+1)
                                            %KMenuCount;

                                        window.Invalidate();
                                    }
                                    else if (
                                        key==EKeyDevice3 ||
                                        key==EKeyEnter
                                    ) {
                                        // Menu action placeholders.
                                        // About N-GBA exits the GUI.
                                        if (selected==5) {
                                            running=EFalse;
                                        }
                                    }
                                    else if (
                                        key==EKeyBackspace ||
                                        key==EKeyEscape
                                    ) {
                                        running=EFalse;
                                    }
                                }

                                if (running) {
                                    session.EventReady(&events);
                                }
                            }

                            // Redraw events
                            if (redraws!=KRequestPending) {
                                if (redraws.Int()!=KErrNone) {
                                    err=redraws.Int();
                                    break;
                                }

                                TWsRedrawEvent redraw;

                                session.GetRedraw(redraw);

                                if (redraw.Handle()==2) {
                                    window.BeginRedraw(
                                        redraw.Rect()
                                    );

                                    gc.Activate(window);

                                    DrawGui(
                                        gc,
                                        size,
                                        selected
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

                        // Cancel outstanding requests
                        session.EventReadyCancel();
                        session.RedrawReadyCancel();

                        if (events==KRequestPending) {
                            User::WaitForRequest(events);
                        }

                        if (redraws==KRequestPending) {
                            User::WaitForRequest(redraws);
                        }

                        window.Close();
                    }

                    group.Close();
                }
            }
        }
    }

    // Close session after screen and graphics objects are destroyed.
    session.Close();

    return err;
}

} // namespace

// ---------------------------------------------------------
// Symbian application entry point
// ---------------------------------------------------------

GLDEF_C TInt E32Main() {
    CTrapCleanup* cleanup=CTrapCleanup::New();

    if (!cleanup) {
        return KErrNoMemory;
    }

    const TInt result=RunGui();

    delete cleanup;

    return result;
}

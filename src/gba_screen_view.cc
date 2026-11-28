
#include "gba_screen_view.h"
#include "gba_ppu.h"

namespace
{
    CFbsBitmap* gFrameBitmap = NULL;

    TBool gOwnsFbsSession = EFalse;
    TInt gInitError = KErrNotReady;

    TBuf<64> gStatus;
    TBuf<64> gStatus2;

    // Posisi teks status (dihitung ulang oleh BuildControls).
    TInt gStatusX = 6;
    TInt gStatusY1 = 0;
    TInt gStatusY2 = 0;

    // Tinggi minimum area kontrol agar tombol layar ditampilkan.
    const TInt KMinControlsHeight = 110;

    // Tinggi yang dicadangkan untuk tombol saat layar game dihitung.
    const TInt KReservedControls = 150;

    struct TControl
    {
        TInt x0;
        TInt y0;
        TInt x1;
        TInt y1;
        TUint16 mask;
        TInt label;     // indeks label
    };

    const TInt KMaxControls = 24;

    // Label tombol (indeks dipakai TControl::label).
    enum
    {
        ELblNone = 0,
        ELblUp,
        ELblDown,
        ELblLeft,
        ELblRight,
        ELblA,
        ELblB,
        ELblL,
        ELblR,
        ELblStart,
        ELblSelect,
        ELblMenu,
        ELblAB
    };

    void AddControl(
        TControl* aList,
        TInt& aCount,
        TInt aX0,
        TInt aY0,
        TInt aX1,
        TInt aY1,
        TUint16 aMask,
        TInt aLabel
    )
    {
        TControl& c = aList[aCount++];
        c.x0 = aX0;
        c.y0 = aY0;
        c.x1 = aX1;
        c.y1 = aY1;
        c.mask = aMask;
        c.label = aLabel;
    }

    TInt MinI(TInt a, TInt b) { return a < b ? a : b; }
    TInt MaxI(TInt a, TInt b) { return a > b ? a : b; }

    TRect ComputeGameRect(const TSize& aSize)
    {
        TInt availH = aSize.iHeight;

        // Sisakan ruang untuk tombol sentuh bila layar cukup tinggi.
        if (aSize.iHeight >= 300)
            availH = aSize.iHeight - KReservedControls;

        TInt gameW = MinI(aSize.iWidth, (availH * 3) / 2);

        // Pakai kelipatan bulat 240 px agar piksel tajam (mis. 480x320).
        if (gameW >= 480)
            gameW = (gameW / 240) * 240;

        if (gameW < 240)
            gameW = MinI(aSize.iWidth, 240);

        const TInt gameH = (gameW * 160) / 240;
        const TInt x = (aSize.iWidth - gameW) / 2;

        return TRect(TPoint(x, 0), TSize(gameW, gameH));
    }

    TInt BuildControls(const TSize& aSize, TControl* aOut)
    {
        const TRect game = ComputeGameRect(aSize);
        const TInt top = game.iBr.iY;
        const TInt h = aSize.iHeight - top;
        const TInt w = aSize.iWidth;

        if (h < KMinControlsHeight)
            return 0;

        TInt n = 0;

        const TBool compact = (h < 200);

        // Baris bahu (L, R, MENU).
        const TInt sh = MaxI(h / 7, 30);
        const TInt shW = w / 4;

        AddControl(aOut, n, 6, top + 2, 6 + shW, top + sh, KNgbaKeyL, ELblL);
        AddControl(aOut, n, w - 6 - shW, top + 2, w - 6, top + sh, KNgbaKeyR, ELblR);
        AddControl(aOut, n, w / 2 - shW / 2, top + 2, w / 2 + shW / 2, top + sh, KNgbaKeyMenu, ELblMenu);

        // D-pad 3x3 (sel diagonal = dua arah sekaligus).
        const TInt padTop = top + sh + 4;
        const TInt padH = h - sh - 8;
        TInt d = MinI(padH / 3, (w * 2 / 5) / 3);
        d = MaxI(d, 20);

        const TInt padX = 6;
        const TInt padY = padTop + (padH - 3 * d) / 2;

        for (TInt row = 0; row < 3; ++row)
        {
            for (TInt col = 0; col < 3; ++col)
            {
                if (row == 1 && col == 1)
                    continue;

                TUint16 mask = 0;
                TInt label = ELblNone;

                if (row == 0) { mask |= KNgbaKeyUp; label = ELblUp; }
                if (row == 2) { mask |= KNgbaKeyDown; label = ELblDown; }
                if (col == 0) { mask |= KNgbaKeyLeft; label = ELblLeft; }
                if (col == 2) { mask |= KNgbaKeyRight; label = ELblRight; }

                // Sel diagonal tidak diberi label.
                if ((row != 1) && (col != 1))
                    label = ELblNone;

                AddControl(aOut, n, padX + col * d, padY + row * d, padX + (col + 1) * d, padY + (row + 1) * d, mask, label);
            }
        }

        // Tombol A dan B (kanan), sel tengah = A+B.
        const TInt btn = MinI(d, (w / 6));
        const TInt btnSize = MaxI(btn, 40);
        const TInt midY = padY + d + d / 2;

        const TInt aX = w - 8 - btnSize;
        const TInt aY = midY - btnSize - btnSize / 4;
        const TInt bX = aX - btnSize - btnSize / 2;
        const TInt bY = midY - btnSize / 4;

        AddControl(aOut, n, aX, aY, aX + btnSize, aY + btnSize, KNgbaKeyA, ELblA);
        AddControl(aOut, n, bX, bY, bX + btnSize, bY + btnSize, KNgbaKeyB, ELblB);

        // A+B: persegi kecil di antara keduanya.
        const TInt abSize = btnSize / 2;
        const TInt abX = bX + btnSize - abSize / 2 + (aX - bX - btnSize) / 2;
        const TInt abY = bY - abSize - 4;
        AddControl(aOut, n, abX, abY, abX + abSize, abY + abSize, static_cast<TUint16>(KNgbaKeyA | KNgbaKeyB), ELblAB);

        // Start dan Select di bawah.
        const TInt ssW = MaxI(w / 6, 50);
        const TInt ssH = MaxI(h / 8, 24);
        TInt ssY;

        if (compact)
        {
            // Layar lanskap: status di tengah, di antara D-pad dan A/B.
            gStatusX = padX + 3 * d + 14;
            gStatusY1 = top + sh + 28;
            gStatusY2 = top + sh + 48;
            ssY = aSize.iHeight - 6 - ssH;
        }
        else
        {
            gStatusX = 6;
            gStatusY1 = aSize.iHeight - 6;
            gStatusY2 = aSize.iHeight - 24;
            ssY = aSize.iHeight - 6 - ssH - 36;
        }
        // Jangan menimpa D-pad: geser ke kanan jika perlu.
        const TInt minX = padX + 3 * d + 8;
        TInt selX = MaxI((w / 2) - ssW - 6, minX);
        TInt startX = selX + ssW + 12;

        if (startX + ssW > w - 6)
        {
            // Layar sempit: rapatkan ke tepi kanan.
            startX = w - 6 - ssW;
            selX = startX - ssW - 12;
        }

        AddControl(aOut, n, selX, ssY, selX + ssW, ssY + ssH,
                   KNgbaKeySelect, ELblSelect);
        AddControl(aOut, n, startX, ssY, startX + ssW, ssY + ssH,
                   KNgbaKeyStart, ELblStart);

        return n;
    }

    void DrawLabel(CWindowGc& aGc, const TControl& aCtl, const TDesC& aText)
    {
        aGc.SetPenColor(TRgb(0xFF, 0xFF, 0xFF));
        aGc.DrawText(
            aText,
            TRect(TPoint(aCtl.x0, aCtl.y0), TPoint(aCtl.x1, aCtl.y1)),
            (aCtl.y1 - aCtl.y0) / 2 + 6,
            CGraphicsContext::ECenter
        );
    }

    void DrawControls(
        CWindowGc& aGc,
        const TSize& aSize,
        CFont* aFont,
        TUint16 aPressed
    )
    {
        TControl controls[KMaxControls];
        const TInt count = BuildControls(aSize, controls);

        if (count == 0)
            return;

        _LIT(KUp, "^");
        _LIT(KDown, "v");
        _LIT(KLeft, "<");
        _LIT(KRight, ">");
        _LIT(KA, "A");
        _LIT(KB, "B");
        _LIT(KL, "L");
        _LIT(KR, "R");
        _LIT(KStart, "START");
        _LIT(KSelect, "SELECT");
        _LIT(KMenu, "MENU");
        _LIT(KAB, "AB");

        if (aFont != NULL)
            aGc.UseFont(aFont);

        aGc.SetBrushStyle(CGraphicsContext::ESolidBrush);
        aGc.SetPenStyle(CGraphicsContext::ESolidPen);

        for (TInt i = 0; i < count; ++i)
        {
            const TControl& c = controls[i];

            const TBool pressed =
                (c.mask != KNgbaKeyMenu) &&
                ((aPressed & c.mask) == c.mask);

            aGc.SetBrushColor(
                pressed ? TRgb(0x30, 0x90, 0xFF) : TRgb(0x28, 0x2C, 0x38)
            );
            aGc.SetPenColor(TRgb(0x60, 0x68, 0x80));

            aGc.DrawRect(
                TRect(TPoint(c.x0, c.y0), TPoint(c.x1, c.y1))
            );

            if (aFont == NULL)
                continue;

            switch (c.label)
            {
            case ELblUp:     DrawLabel(aGc, c, KUp); break;
            case ELblDown:   DrawLabel(aGc, c, KDown); break;
            case ELblLeft:   DrawLabel(aGc, c, KLeft); break;
            case ELblRight:  DrawLabel(aGc, c, KRight); break;
            case ELblA:      DrawLabel(aGc, c, KA); break;
            case ELblB:      DrawLabel(aGc, c, KB); break;
            case ELblL:      DrawLabel(aGc, c, KL); break;
            case ELblR:      DrawLabel(aGc, c, KR); break;
            case ELblStart:  DrawLabel(aGc, c, KStart); break;
            case ELblSelect: DrawLabel(aGc, c, KSelect); break;
            case ELblMenu:   DrawLabel(aGc, c, KMenu); break;
            case ELblAB:     DrawLabel(aGc, c, KAB); break;
            default: break;
            }
        }

        if (aFont != NULL)
            aGc.DiscardFont();
    }

    // Menyalin framebuffer RGB555 ke bitmap XRGB8888 240x160.
    TBool ConvertFrame()
    {
        const TUint16* source = NgbaPpuGetFrameBufferRgb555();

        if (source == NULL || gFrameBitmap == NULL)
            return EFalse;

        gFrameBitmap->LockHeap();

        TUint8* base =
            reinterpret_cast<TUint8*>(gFrameBitmap->DataAddress());

        if (base == NULL)
        {
            gFrameBitmap->UnlockHeap();
            return EFalse;
        }

        const TInt stride = gFrameBitmap->DataStride();

        for (TInt y = 0; y < KGbaScreenHeight; ++y)
        {
            const TUint16* row = source + y * KGbaScreenWidth;
            TUint32* dst = reinterpret_cast<TUint32*>(base + y * stride);

            for (TInt x = 0; x < KGbaScreenWidth; ++x)
            {
                const TUint32 c = row[x] & 0x7FFFu;

                const TUint32 r5 = c & 0x1Fu;
                const TUint32 g5 = (c >> 5) & 0x1Fu;
                const TUint32 b5 = (c >> 10) & 0x1Fu;

                const TUint32 r8 = (r5 << 3) | (r5 >> 2);
                const TUint32 g8 = (g5 << 3) | (g5 >> 2);
                const TUint32 b8 = (b5 << 3) | (b5 >> 2);

                dst[x] = 0xFF000000u | (r8 << 16) | (g8 << 8) | b8;
            }
        }

        gFrameBitmap->UnlockHeap();
        return ETrue;
    }
}

// --------------------------------------------------
// INISIALISASI
// --------------------------------------------------

TInt NgbaScreenViewInitialize()
{
    if (gFrameBitmap != NULL)
        return KErrNone;

    // Aplikasi ini memakai RWsSession langsung (bukan framework CONE),
    // sehingga sesi Font & Bitmap Server HARUS disambungkan sendiri.
    // Tanpa ini CFbsBitmap::Create() gagal dan layar game tetap putih.
    if (RFbsSession::GetSession() == NULL)
    {
        const TInt connectError = RFbsSession::Connect();

        if (connectError != KErrNone)
        {
            gInitError = connectError;
            return connectError;
        }

        gOwnsFbsSession = ETrue;
    }

    gFrameBitmap = new CFbsBitmap();

    if (gFrameBitmap == NULL)
    {
        gInitError = KErrNoMemory;
        return KErrNoMemory;
    }

    const TInt error = gFrameBitmap->Create(
        TSize(KGbaScreenWidth, KGbaScreenHeight),
        EColor16MU
    );

    if (error != KErrNone)
    {
        delete gFrameBitmap;
        gFrameBitmap = NULL;
        gInitError = error;
        return error;
    }

    gInitError = KErrNone;
    return KErrNone;
}

void NgbaScreenViewShutdown()
{
    delete gFrameBitmap;
    gFrameBitmap = NULL;

    if (gOwnsFbsSession)
    {
        RFbsSession::Disconnect();
        gOwnsFbsSession = EFalse;
    }
}

TInt NgbaScreenViewError()
{
    return gInitError;
}

void NgbaScreenViewSetStatus(const TDesC& aText)
{
    gStatus.Copy(aText.Left(gStatus.MaxLength()));
}

void NgbaScreenViewSetStatus2(const TDesC& aText)
{
    gStatus2.Copy(aText.Left(gStatus2.MaxLength()));
}

TRect NgbaScreenViewGameRect(const TSize& aSize)
{
    return ComputeGameRect(aSize);
}

TRect NgbaScreenViewControlsRect(const TSize& aSize)
{
    const TRect game = ComputeGameRect(aSize);

    return TRect(
        TPoint(0, game.iBr.iY),
        TPoint(aSize.iWidth, aSize.iHeight)
    );
}

TUint16 NgbaScreenViewHitTest(
    const TPoint& aPoint,
    const TSize& aSize
)
{
    TControl controls[KMaxControls];
    const TInt count = BuildControls(aSize, controls);

    for (TInt i = 0; i < count; ++i)
    {
        const TControl& c = controls[i];

        if (aPoint.iX >= c.x0 && aPoint.iX < c.x1 &&
            aPoint.iY >= c.y0 && aPoint.iY < c.y1)
        {
            return c.mask;
        }
    }

    return 0;
}

// --------------------------------------------------
// MENGGAMBAR
// --------------------------------------------------

void NgbaScreenViewDraw(
    CWindowGc& aGc,
    const TRect& aTargetRect,
    CFont* aFont,
    TUint16 aPressedMask
)
{
    const TSize size = aTargetRect.Size();

    // Latar hitam di seluruh area (tidak ada lagi layar putih bawaan).
    aGc.SetBrushStyle(CGraphicsContext::ESolidBrush);
    aGc.SetBrushColor(KRgbBlack);
    aGc.SetPenStyle(CGraphicsContext::ENullPen);
    aGc.Clear(aTargetRect);
    aGc.SetPenStyle(CGraphicsContext::ESolidPen);

    const TRect gameRect = ComputeGameRect(size);

    if (gFrameBitmap != NULL && ConvertFrame())
    {
        aGc.DrawBitmap(gameRect, gFrameBitmap);
    }
    else
    {
        // Bitmap gagal dibuat: tampilkan pesan kesalahan, bukan layar kosong.
        aGc.SetBrushColor(TRgb(0x80, 0x00, 0x00));
        aGc.SetPenColor(KRgbWhite);
        aGc.DrawRect(gameRect);

        if (aFont != NULL)
        {
            TBuf<48> msg;
            msg.Copy(_L("BITMAP ERR "));
            msg.AppendNum(gInitError);

            aGc.UseFont(aFont);
            aGc.DrawText(msg, TPoint(gameRect.iTl.iX + 8, gameRect.iTl.iY + 40));
            aGc.DiscardFont();
        }
    }

    // Tombol sentuh + status.
    gStatusX = 6;
    gStatusY1 = size.iHeight - 6;
    gStatusY2 = size.iHeight - 24;

    DrawControls(aGc, size, aFont, aPressedMask);

    if (aFont != NULL)
    {
        aGc.UseFont(aFont);
        aGc.SetPenColor(TRgb(0xA0, 0xA8, 0xC0));
        aGc.SetBrushStyle(CGraphicsContext::ENullBrush);

        if (gStatus.Length() > 0)
            aGc.DrawText(gStatus, TPoint(gStatusX, gStatusY1));

        if (gStatus2.Length() > 0)
            aGc.DrawText(gStatus2, TPoint(gStatusX, gStatusY2));

        aGc.DiscardFont();
    }
}

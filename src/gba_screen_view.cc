
#include "gba_screen_view.h"
#include "gba_ppu.h"

namespace
{
    const TInt KScaleFactor = 2;

    const TInt KScaledWidth =
        KGbaScreenWidth * KScaleFactor;

    const TInt KScaledHeight =
        KGbaScreenHeight * KScaleFactor;

    CFbsBitmap* gFrameBitmap = NULL;
}

// --------------------------------------------------
// INISIALISASI BITMAP
// --------------------------------------------------

TInt NgbaScreenViewInitialize()
{
    if (gFrameBitmap != NULL)
    {
        return KErrNone;
    }

    gFrameBitmap = new CFbsBitmap();

    if (gFrameBitmap == NULL)
    {
        return KErrNoMemory;
    }

    const TInt error = gFrameBitmap->Create(
        TSize(KScaledWidth, KScaledHeight),
        EColor16MU
    );

    if (error != KErrNone)
    {
        delete gFrameBitmap;
        gFrameBitmap = NULL;
        return error;
    }

    return KErrNone;
}

// --------------------------------------------------
// PEMBERSIHAN BITMAP
// --------------------------------------------------

void NgbaScreenViewShutdown()
{
    delete gFrameBitmap;
    gFrameBitmap = NULL;
}

// --------------------------------------------------
// MENGGAMBAR FRAMEBUFFER
// --------------------------------------------------

void NgbaScreenViewDraw(
    CWindowGc& aGc,
    const TRect& aTargetRect
)
{
    if (gFrameBitmap == NULL)
    {
        return;
    }

    const TUint16* source =
        NgbaPpuGetFrameBufferRgb555();

    if (source == NULL)
    {
        return;
    }

    // Akses memori bitmap.
    gFrameBitmap->LockHeap();

    TUint32* destination =
        reinterpret_cast<TUint32*>(
            gFrameBitmap->DataAddress()
        );

    if (destination == NULL)
    {
        gFrameBitmap->UnlockHeap();
        return;
    }

    // RGB555 -> XRGB8888 dan scaling 2x.
    for (TInt y = 0; y < KGbaScreenHeight; ++y)
    {
        const TUint16* sourceRow =
            source + (y * KGbaScreenWidth);

        TUint32* destinationRow0 =
            destination + (y * 2 * KScaledWidth);

        TUint32* destinationRow1 =
            destinationRow0 + KScaledWidth;

        for (TInt x = 0; x < KGbaScreenWidth; ++x)
        {
            const TUint16 color =
                sourceRow[x] & 0x7FFFu;

            const TUint32 r5 = color & 0x1Fu;
            const TUint32 g5 = (color >> 5) & 0x1Fu;
            const TUint32 b5 = (color >> 10) & 0x1Fu;

            const TUint32 r8 =
                (r5 << 3) | (r5 >> 2);

            const TUint32 g8 =
                (g5 << 3) | (g5 >> 2);

            const TUint32 b8 =
                (b5 << 3) | (b5 >> 2);

            const TUint32 pixel =
                (r8 << 16) |
                (g8 << 8) |
                b8;

            const TInt dx = x * 2;

            destinationRow0[dx] = pixel;
            destinationRow0[dx + 1] = pixel;

            destinationRow1[dx] = pixel;
            destinationRow1[dx + 1] = pixel;
        }
    }

    gFrameBitmap->UnlockHeap();

    // Bersihkan area layar dengan warna hitam.
    aGc.SetBrushStyle(
        CGraphicsContext::ESolidBrush
    );

    aGc.SetBrushColor(KRgbBlack);
    aGc.Clear(aTargetRect);

    // Pusatkan gambar 480 x 320.
    const TInt x =
        aTargetRect.iTl.iX +
        (aTargetRect.Width() - KScaledWidth) / 2;

    const TInt y =
        aTargetRect.iTl.iY +
        (aTargetRect.Height() - KScaledHeight) / 2;

    // Kirim bitmap ke Window Server.
    aGc.BitBlt(
        TPoint(x, y),
        gFrameBitmap
    );
}

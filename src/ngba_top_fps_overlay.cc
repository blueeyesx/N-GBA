#include "ngba_top_fps_overlay.h"
#include <gdi.h>
#include <eikenv.h>

CNgbaTopFpsOverlay* CNgbaTopFpsOverlay::NewL(CCoeControl& aParent, const TRect& aScreenRect)
{
    CNgbaTopFpsOverlay* self = NewLC(aParent, aScreenRect);
    CleanupStack::Pop(self);
    return self;
}

CNgbaTopFpsOverlay* CNgbaTopFpsOverlay::NewLC(CCoeControl& aParent, const TRect& aScreenRect)
{
    CNgbaTopFpsOverlay* self = new (ELeave) CNgbaTopFpsOverlay();
    CleanupStack::PushL(self);
    self->ConstructL(aParent, aScreenRect);
    return self;
}

CNgbaTopFpsOverlay::CNgbaTopFpsOverlay()
{
}

CNgbaTopFpsOverlay::~CNgbaTopFpsOverlay()
{
}

void CNgbaTopFpsOverlay::ConstructL(CCoeControl& aParent, const TRect& aScreenRect)
{
    SetContainerWindowL(aParent);
    // Pojok kiri atas layar, di luar area tengah/bawah layar game.
    const TInt marginX = 4;
    const TInt marginY = 2;
    const TInt width = 68;
    const TInt height = 18;
    SetRect(TRect(
        TPoint(aScreenRect.iTl.iX + marginX, aScreenRect.iTl.iY + marginY),
        TSize(width, height)
    ));
    SetShowFps(NgbaPpuGetShowFps());
    ActivateL();
}

void CNgbaTopFpsOverlay::SetShowFps(TBool aEnabled)
{
    NgbaPpuSetShowFps(aEnabled);
    MakeVisible(aEnabled);
    if (aEnabled)
    {
        DrawNow();
    }
}

TBool CNgbaTopFpsOverlay::ShowFps() const
{
    return NgbaPpuGetShowFps();
}

void CNgbaTopFpsOverlay::RefreshFps()
{
    if (!NgbaPpuGetShowFps())
    {
        if (IsVisible())
            MakeVisible(EFalse);
        return;
    }

    if (!IsVisible())
        MakeVisible(ETrue);

    DrawNow();
}

void CNgbaTopFpsOverlay::Draw(const TRect& /*aRect*/) const
{
    if (!NgbaPpuGetShowFps())
        return;

    CWindowGc& gc = SystemGc();
    const TRect r = Rect();

    gc.SetPenStyle(CGraphicsContext::ENullPen);
    gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
    gc.SetBrushColor(TRgb(0, 0, 0));
    gc.DrawRect(r);

    TBuf<24> fpsText;
    NgbaPpuGetFpsText(fpsText);

    const CFont* font = CEikonEnv::Static()->DenseFont();
    gc.UseFont(font);
    gc.SetPenStyle(CGraphicsContext::ESolidPen);
    gc.SetPenColor(TRgb(0, 255, 90));

    const TInt baselineY = r.iTl.iY + ((r.Height() + font->AscentInPixels()) / 2);
    gc.DrawText(fpsText, TPoint(r.iTl.iX + 4, baselineY));
    gc.DiscardFont();
}

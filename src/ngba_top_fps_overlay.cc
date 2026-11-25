#include "ngba_top_fps_overlay.h"
#include <gdi.h>
#include <eikenv.h>

void NgbaDrawTopLeftFpsToGc(CGraphicsContext& aGc, const TRect& aScreenRect)
{
    if (!NgbaPpuGetShowFps())
        return;

    TBuf<24> fpsText;
    NgbaPpuGetFpsText(fpsText);
    if (fpsText.Length() == 0)
        return;

    const CFont* font = CEikonEnv::Static()->DenseFont();
    if (!font)
        return;

    const TInt textW = font->TextWidthInPixels(fpsText);
    const TInt boxW = textW + 8;
    const TInt boxH = font->HeightInPixels() + 4;

    const TRect boxRect(
        TPoint(aScreenRect.iTl.iX + 2, aScreenRect.iTl.iY + 2),
        TSize(boxW, boxH)
    );

    aGc.SetClippingRect(boxRect);
    aGc.SetPenStyle(CGraphicsContext::ENullPen);
    aGc.SetBrushStyle(CGraphicsContext::ESolidBrush);
    aGc.SetBrushColor(TRgb(0, 0, 0));
    aGc.DrawRect(boxRect);

    aGc.UseFont(font);
    aGc.SetPenStyle(CGraphicsContext::ESolidPen);
    aGc.SetPenColor(TRgb(0, 255, 90));

    const TInt baselineY = boxRect.iTl.iY + ((boxRect.Height() + font->AscentInPixels()) / 2);
    aGc.DrawText(fpsText, TPoint(boxRect.iTl.iX + 4, baselineY));
    aGc.DiscardFont();
    aGc.CancelClippingRect();
}

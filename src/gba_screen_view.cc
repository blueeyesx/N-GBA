#include "ngba_top_fps_overlay.h"
#include <eikenv.h>
#include <w32std.h>

namespace
{
    const TUint8 KGlyph0[7]   = { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E };
    const TUint8 KGlyph1[7]   = { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E };
    const TUint8 KGlyph2[7]   = { 0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F };
    const TUint8 KGlyph3[7]   = { 0x1F, 0x02, 0x04, 0x06, 0x01, 0x11, 0x0E };
    const TUint8 KGlyph4[7]   = { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 };
    const TUint8 KGlyph5[7]   = { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E };
    const TUint8 KGlyph6[7]   = { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E };
    const TUint8 KGlyph7[7]   = { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 };
    const TUint8 KGlyph8[7]   = { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E };
    const TUint8 KGlyph9[7]   = { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x1C };
    const TUint8 KGlyphF[7]   = { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 };
    const TUint8 KGlyphP[7]   = { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 };
    const TUint8 KGlyphS[7]   = { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E };
    const TUint8 KGlyphCol[7] = { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 };

    const TUint8* GetGlyph5x7(TChar aCh)
    {
        switch (aCh)
        {
        case '0': return KGlyph0;
        case '1': return KGlyph1;
        case '2': return KGlyph2;
        case '3': return KGlyph3;
        case '4': return KGlyph4;
        case '5': return KGlyph5;
        case '6': return KGlyph6;
        case '7': return KGlyph7;
        case '8': return KGlyph8;
        case '9': return KGlyph9;
        case 'F': return KGlyphF;
        case 'P': return KGlyphP;
        case 'S': return KGlyphS;
        case ':': return KGlyphCol;
        default:  return NULL;
        }
    }

    void BuildFpsChars(TChar* aOutChars, TInt& aOutLen)
    {
        aOutLen = 0;
        aOutChars[aOutLen++] = 'F';
        aOutChars[aOutLen++] = 'P';
        aOutChars[aOutLen++] = 'S';
        aOutChars[aOutLen++] = ':';

        TInt fpsVal = NgbaPpuGetFps();
        if (fpsVal < 0) fpsVal = 0;
        if (fpsVal > 999) fpsVal = 999;

        if (fpsVal >= 100)
            aOutChars[aOutLen++] = static_cast<TChar>('0' + ((fpsVal / 100) % 10));
        if (fpsVal >= 10)
            aOutChars[aOutLen++] = static_cast<TChar>('0' + ((fpsVal / 10) % 10));
        aOutChars[aOutLen++] = static_cast<TChar>('0' + (fpsVal % 10));
    }
}

void NgbaDrawTopLeftFpsToBitmap(CFbsBitmap& aScreenBitmap)
{
    if (!NgbaPpuGetShowFps())
        return;

    const TSize sz = aScreenBitmap.SizeInPixels();
    if (sz.iWidth < 80 || sz.iHeight < 30)
        return;

    const TDisplayMode mode = aScreenBitmap.DisplayMode();
    if (mode != EColor64K && mode != EColor16MU && mode != EColor16MA)
        return;

    TChar chars[10];
    TInt count = 0;
    BuildFpsChars(chars, count);

    const TInt scale = (sz.iWidth >= 480) ? 2 : 1;
    const TInt startX = 6;
    const TInt startY = 6;
    const TInt boxW = (count * 6 + 3) * scale;
    const TInt boxH = 11 * scale;

    aScreenBitmap.BeginDataAccess();
    TUint8* base = reinterpret_cast<TUint8*>(aScreenBitmap.DataAddress());
    const TInt stride = aScreenBitmap.ScanLineLength(sz.iWidth, mode);

    if (base != NULL)
    {
        for (TInt y = startY; y < startY + boxH && y < sz.iHeight; ++y)
        {
            if (mode == EColor64K)
            {
                TUint16* row = reinterpret_cast<TUint16*>(base + y * stride);
                for (TInt x = startX; x < startX + boxW && x < sz.iWidth; ++x)
                    row[x] = 0x0000u;
            }
            else
            {
                TUint32* row = reinterpret_cast<TUint32*>(base + y * stride);
                for (TInt x = startX; x < startX + boxW && x < sz.iWidth; ++x)
                    row[x] = 0xFF000000u;
            }
        }

        const TInt textX = startX + 2 * scale;
        const TInt textY = startY + 2 * scale;

        for (TInt i = 0; i < count; ++i)
        {
            const TUint8* glyph = GetGlyph5x7(chars[i]);
            if (!glyph)
                continue;

            const TInt gx0 = textX + i * 6 * scale;
            for (TInt r = 0; r < 7; ++r)
            {
                const TUint8 bits = glyph[r];
                for (TInt c = 0; c < 5; ++c)
                {
                    if (!(bits & (1u << (4 - c))))
                        continue;

                    for (TInt sy = 0; sy < scale; ++sy)
                    {
                        const TInt py = textY + r * scale + sy;
                        if (py < 0 || py >= sz.iHeight)
                            continue;

                        if (mode == EColor64K)
                        {
                            TUint16* row = reinterpret_cast<TUint16*>(base + py * stride);
                            for (TInt sx = 0; sx < scale; ++sx)
                            {
                                const TInt px = gx0 + c * scale + sx;
                                if (px >= 0 && px < sz.iWidth)
                                    row[px] = 0x07E0u;
                            }
                        }
                        else
                        {
                            TUint32* row = reinterpret_cast<TUint32*>(base + py * stride);
                            for (TInt sx = 0; sx < scale; ++sx)
                            {
                                const TInt px = gx0 + c * scale + sx;
                                if (px >= 0 && px < sz.iWidth)
                                    row[px] = 0xFF00FF5Au;
                            }
                        }
                    }
                }
            }
        }
    }

    aScreenBitmap.EndDataAccess();
}

void NgbaDrawTopLeftFpsToGc(CGraphicsContext& aGc, const TRect& aScreenRect)
{
    if (!NgbaPpuGetShowFps())
        return;

    TChar chars[10];
    TInt count = 0;
    BuildFpsChars(chars, count);

    const TInt scale = (aScreenRect.Width() >= 480) ? 2 : 1;
    const TInt startX = aScreenRect.iTl.iX + 6;
    const TInt startY = aScreenRect.iTl.iY + 6;
    const TInt boxW = (count * 6 + 3) * scale;
    const TInt boxH = 11 * scale;

    aGc.SetPenStyle(CGraphicsContext::ENullPen);
    aGc.SetBrushStyle(CGraphicsContext::ESolidBrush);
    aGc.SetBrushColor(TRgb(0, 0, 0));
    aGc.DrawRect(TRect(TPoint(startX, startY), TSize(boxW, boxH)));

    aGc.SetBrushColor(TRgb(0, 255, 90));
    const TInt textX = startX + 2 * scale;
    const TInt textY = startY + 2 * scale;

    for (TInt i = 0; i < count; ++i)
    {
        const TUint8* glyph = GetGlyph5x7(chars[i]);
        if (!glyph)
            continue;

        const TInt gx0 = textX + i * 6 * scale;
        for (TInt r = 0; r < 7; ++r)
        {
            const TUint8 bits = glyph[r];
            for (TInt c = 0; c < 5; ++c)
            {
                if (bits & (1u << (4 - c)))
                {
                    aGc.DrawRect(TRect(
                        TPoint(gx0 + c * scale, textY + r * scale),
                        TSize(scale, scale)
                    ));
                }
            }
        }
    }
}

void NgbaDrawTopLeftFpsAuto()
{
    if (!NgbaPpuGetShowFps())
        return;

    CEikonEnv* env = CEikonEnv::Static();
    if (!env)
        return;

    CWsScreenDevice* screenDev = env->ScreenDevice();
    if (!screenDev)
        return;

    const TSize scrSize = screenDev->SizeInPixels();
    CWindowGc& gc = env->SystemGc();
    gc.Activate(*env->RootWin());
    NgbaDrawTopLeftFpsToGc(gc, TRect(TPoint(0, 0), scrSize));
    gc.Deactivate();
}

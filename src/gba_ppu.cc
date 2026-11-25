#include "gba_ppu.h"
#include "gba_memory.h"
#include <f32file.h>

namespace
{
    // Alamat register I/O PPU & Interrupt.
    const TUint32 KRegDispCnt   = 0x04000000u;
    const TUint32 KRegGreenSwap = 0x04000002u;
    const TUint32 KRegDispStat  = 0x04000004u;
    const TUint32 KRegVCount    = 0x04000006u;
    const TUint32 KRegIf        = 0x04000202u;

    // Alamat basis memori grafis GBA.
    const TUint32 KPalRamBase   = 0x05000000u;
    const TUint32 KVramBase     = 0x06000000u;
    const TUint32 KOamBase      = 0x07000000u;

    // Konstanta timing PPU GBA (16.78 MHz).
    const TInt KCyclesHDraw     = 960;
    const TInt KCyclesHBlank    = 272;
    const TInt KVisibleLines    = 160;
    const TInt KTotalLines      = 228;

    // Bit bendera IRQ pada register IE / IF.
    const TUint16 KIrqVBlank    = 0x0001u;
    const TUint16 KIrqHBlank    = 0x0002u;
    const TUint16 KIrqVCount    = 0x0004u;

    // Penanda piksel transparan di buffer komposisi scanline.
    const TUint16 KTransparent  = 0x8000u;

    // Indeks lapisan komposisi.
    const TInt KLayerBg0        = 0;
    const TInt KLayerBg1        = 1;
    const TInt KLayerBg2        = 2;
    const TInt KLayerBg3        = 3;
    const TInt KLayerObj        = 4;
    const TInt KLayerBackdrop   = 5;

    struct TPpuState
    {
        TUint16 dispCnt;
        TUint16 greenSwap;
        TUint16 dispStat;
        TUint16 vCount;

        TUint16 bgCnt[4];
        TUint16 bgHOfs[4];
        TUint16 bgVOfs[4];

        // Parameter Affine untuk BG2 (indeks 0) dan BG3 (indeks 1).
        TInt16  bgPa[2];
        TInt16  bgPb[2];
        TInt16  bgPc[2];
        TInt16  bgPd[2];
        TInt32  bgAffX[2];
        TInt32  bgAffY[2];
        TInt32  internalX[2];
        TInt32  internalY[2];

        // Windowing (WIN0, WIN1, WININ, WINOUT).
        TUint16 win0H;
        TUint16 win1H;
        TUint16 win0V;
        TUint16 win1V;
        TUint16 winIn;
        TUint16 winOut;

        // Mosaic & Color Special Effects (Blending / Fade).
        TUint16 mosaic;
        TUint16 bldCnt;
        TUint16 bldAlpha;
        TUint16 bldY;

        TUint16 frameBuffer[KGbaScreenPixels];
    };

    TPpuState gPpu;

    // ---------------------------------------------------------
    // PPU LOGGING
    // ---------------------------------------------------------

    TUint32 gPpuFrameCounter = 0;
    TUint16 gLastLoggedDispCnt = 0xFFFFu;

    _LIT(KPpuLogFolder, "E:\\GBA\\");
    _LIT(KPpuLogFile, "E:\\GBA\\ngba.log");

    void WritePpuLog(const TDesC& aMessage)
    {
        RFs fs;

        if (fs.Connect() != KErrNone)
            return;

        TInt error = fs.MkDirAll(KPpuLogFolder);

        if (error != KErrNone && error != KErrAlreadyExists)
        {
            fs.Close();
            return;
        }

        RFile file;

        error = file.Open(
            fs,
            KPpuLogFile,
            EFileWrite | EFileShareAny
        );

        if (error == KErrNotFound ||
            error == KErrPathNotFound)
        {
            error = file.Create(
                fs,
                KPpuLogFile,
                EFileWrite | EFileShareAny
            );
        }

        if (error == KErrNone)
        {
            TInt position = 0;

            error = file.Seek(ESeekEnd, position);

            if (error == KErrNone)
            {
                TBuf8<192> line;

                line.Append(_L8("N-GBA | PPU | "));

                for (TInt i = 0;
                     i < aMessage.Length() &&
                     line.Length() < 180;
                     ++i)
                {
                    const TUint16 ch = aMessage[i];

                    const TUint8 ch8 =
                        (ch <= 255)
                        ? static_cast<TUint8>(ch)
                        : static_cast<TUint8>('?');

                    line.Append(&ch8, 1);
                }

                line.Append(_L8("\r\n"));

                file.Write(line);
            }

            file.Close();
        }

        fs.Close();
    }

    void LogPpuDispCnt(TUint16 aValue)
    {
        TBuf<80> message;

        message.Copy(_L("DISPCNT=0x"));

        message.AppendNum(
            static_cast<TInt>(aValue),
            EHex
        );

        message.Append(_L(" MODE="));

        message.AppendNum(
            static_cast<TInt>(aValue & 7u)
        );

        message.Append(_L(" FORCED_BLANK="));

        message.AppendNum(
            (aValue & 0x0080u) ? 1 : 0
        );

        message.Append(_L(" BG_ENABLE=0x"));

        message.AppendNum(
            static_cast<TInt>((aValue >> 8) & 0x0Fu),
            EHex
        );

        WritePpuLog(message);
    }

    TNgbaPpuIrqCallback   gIrqCallback    = NULL;
    TNgbaPpuEventCallback gVBlankCallback = NULL;
    TNgbaPpuEventCallback gHBlankCallback = NULL;

    // Tabel ukuran sprite [shape][size] -> lebar & tinggi dalam piksel.
    const TInt KObjWidthTable[3][4] =
    {
        {  8, 16, 32, 64 },
        { 16, 32, 32, 64 },
        {  8,  8, 16, 32 }
    };

    const TInt KObjHeightTable[3][4] =
    {
        {  8, 16, 32, 64 },
        {  8,  8, 16, 32 },
        { 16, 32, 32, 64 }
    };

    inline TInt32 SignExtend28(TUint32 aValue)
    {
        aValue &= 0x0FFFFFFFu;

        if (aValue & 0x08000000u)
            aValue |= 0xF0000000u;

        return static_cast<TInt32>(aValue);
    }

    inline TInt ClampInt(TInt aVal, TInt aMin, TInt aMax)
    {
        if (aVal < aMin)
            return aMin;

        if (aVal > aMax)
            return aMax;

        return aVal;
    }

    void RaisePpuIrq(TUint16 aFlag)
    {
        if (gIrqCallback != NULL)
        {
            gIrqCallback(aFlag);
        }
        else
        {
            const TUint16 currentIf =
                NgbaMemoryRead16(KRegIf);

            NgbaMemoryWrite16(
                KRegIf,
                static_cast<TUint16>(currentIf | aFlag)
            );
        }
    }

    void RunCpuCycles(
        TNgbaCpuState& aCpu,
        TInt aTargetCycles
    )
    {
        const TUint32 startCycles = aCpu.cycles;

        while (static_cast<TInt>(
            aCpu.cycles - startCycles
        ) < aTargetCycles)
        {
            const TInt status = NgbaCpuStep(aCpu);

            if (status != ENgbaCpuOk)
                break;
        }
    }

    // ---------------------------------------------------------
    // RENDER REGULAR TILED BACKGROUND (BG0 - BG3)
    // ---------------------------------------------------------

    void RenderTiledBgScanline(
        TInt aBgIndex,
        TInt aY,
        TUint16* aLineBuffer
    )
    {
        const TUint16 cnt = gPpu.bgCnt[aBgIndex];

        const TUint32 charBase =
            ((cnt >> 2) & 3u) * 0x4000u;

        const TUint32 screenBase =
            ((cnt >> 8) & 31u) * 0x800u;

        const TBool is8bpp =
            (cnt & (1u << 7)) != 0;

        const TInt screenSize = (cnt >> 14) & 3;

        const TInt widthMask =
            (screenSize & 1) ? 511 : 255;

        const TInt heightMask =
            (screenSize & 2) ? 511 : 255;

        const TInt scrollX =
            gPpu.bgHOfs[aBgIndex] & 0x1FF;

        const TInt scrollY =
            gPpu.bgVOfs[aBgIndex] & 0x1FF;

        const TInt mapY = (aY + scrollY) & heightMask;
        const TInt tileY = mapY >> 3;
        const TInt fineY = mapY & 7;

        const TUint32 yBlockOffset =
            (screenSize == 2 && tileY >= 32) ? 0x800u :
            (screenSize == 3 && tileY >= 32) ? 0x1000u : 0u;

        for (TInt x = 0; x < KGbaScreenWidth; ++x)
        {
            const TInt mapX = (x + scrollX) & widthMask;
            const TInt tileX = mapX >> 3;
            const TInt fineX = mapX & 7;

            TUint32 blockOffset = yBlockOffset;

            if ((screenSize == 1 || screenSize == 3) &&
                tileX >= 32)
            {
                blockOffset += 0x800u;
            }

            const TUint32 entryAddr =
                KVramBase + screenBase + blockOffset +
                static_cast<TUint32>(
                    ((tileY & 31) * 32 + (tileX & 31)) * 2
                );

            const TUint16 mapEntry =
                NgbaMemoryRead16(entryAddr);

            const TUint32 tileNum = mapEntry & 0x03FFu;
            const TBool hFlip = (mapEntry & (1u << 10)) != 0;
            const TBool vFlip = (mapEntry & (1u << 11)) != 0;
            const TUint32 palBank = (mapEntry >> 12) & 0x0Fu;

            const TInt px = hFlip ? (7 - fineX) : fineX;
            const TInt py = vFlip ? (7 - fineY) : fineY;

            if (is8bpp)
            {
                const TUint32 pixelAddr =
                    KVramBase + charBase + tileNum * 64u +
                    static_cast<TUint32>(py * 8 + px);

                if (pixelAddr >= KVramBase + 0x10000u)
                    continue;

                const TUint8 colorIdx =
                    NgbaMemoryRead8(pixelAddr);

                if (colorIdx != 0)
                {
                    aLineBuffer[x] =
                        NgbaMemoryRead16(
                            KPalRamBase +
                            static_cast<TUint32>(colorIdx) * 2u
                        ) & 0x7FFFu;
                }
            }
            else
            {
                const TUint32 pixelAddr =
                    KVramBase + charBase + tileNum * 32u +
                    static_cast<TUint32>(
                        py * 4 + (px >> 1)
                    );

                if (pixelAddr >= KVramBase + 0x10000u)
                    continue;

                const TUint8 packed =
                    NgbaMemoryRead8(pixelAddr);

                const TUint8 colorIdx = (px & 1)
                    ? static_cast<TUint8>(packed >> 4)
                    : static_cast<TUint8>(packed & 0x0Fu);

                if (colorIdx != 0)
                {
                    const TUint32 palIndex =
                        palBank * 16u + colorIdx;

                    aLineBuffer[x] =
                        NgbaMemoryRead16(
                            KPalRamBase + palIndex * 2u
                        ) & 0x7FFFu;
                }
            }
        }
    }

    // ---------------------------------------------------------
    // RENDER AFFINE BACKGROUND (BG2 / BG3)
    // ---------------------------------------------------------

    void RenderAffineBgScanline(
        TInt aBgIndex,
        TUint16* aLineBuffer
    )
    {
        const TInt affIdx = aBgIndex - 2;

        if (affIdx < 0 || affIdx > 1)
            return;

        const TUint16 cnt = gPpu.bgCnt[aBgIndex];

        const TUint32 charBase =
            ((cnt >> 2) & 3u) * 0x4000u;

        const TUint32 screenBase =
            ((cnt >> 8) & 31u) * 0x800u;

        const TBool wrapAround =
            (cnt & (1u << 13)) != 0;

        const TInt screenSize = (cnt >> 14) & 3;

        const TInt mapPixels = 128 << screenSize;
        const TInt mapMask = mapPixels - 1;
        const TInt tilesPerRow = mapPixels >> 3;

        TInt32 curX = gPpu.internalX[affIdx];
        TInt32 curY = gPpu.internalY[affIdx];

        const TInt32 dmx = gPpu.bgPa[affIdx];
        const TInt32 dmy = gPpu.bgPc[affIdx];

        for (TInt x = 0; x < KGbaScreenWidth; ++x)
        {
            TInt texX = curX >> 8;
            TInt texY = curY >> 8;

            curX += dmx;
            curY += dmy;

            if (wrapAround)
            {
                texX &= mapMask;
                texY &= mapMask;
            }
            else if (texX < 0 || texX >= mapPixels ||
                     texY < 0 || texY >= mapPixels)
            {
                continue;
            }

            const TInt tileX = texX >> 3;
            const TInt tileY = texY >> 3;
            const TInt fineX = texX & 7;
            const TInt fineY = texY & 7;

            const TUint32 mapAddr =
                KVramBase + screenBase +
                static_cast<TUint32>(
                    tileY * tilesPerRow + tileX
                );

            const TUint8 tileNum =
                NgbaMemoryRead8(mapAddr);

            const TUint32 pixelAddr =
                KVramBase + charBase +
                static_cast<TUint32>(tileNum) * 64u +
                static_cast<TUint32>(fineY * 8 + fineX);

            if (pixelAddr >= KVramBase + 0x10000u)
                continue;

            const TUint8 colorIdx =
                NgbaMemoryRead8(pixelAddr);

            if (colorIdx != 0)
            {
                aLineBuffer[x] =
                    NgbaMemoryRead16(
                        KPalRamBase +
                        static_cast<TUint32>(colorIdx) * 2u
                    ) & 0x7FFFu;
            }
        }
    }

    // ---------------------------------------------------------
    // RENDER OAM SPRITES
    // ---------------------------------------------------------

    void RenderSpritesScanline(
        TInt aY,
        TUint16 aObjColor[KGbaScreenWidth],
        TUint8 aObjPrio[KGbaScreenWidth],
        TBool aObjSemiTrans[KGbaScreenWidth],
        TBool aObjWindow[KGbaScreenWidth]
    )
    {
        if ((gPpu.dispCnt & (1u << 12)) == 0)
            return;

        const TBool mapping1D =
            (gPpu.dispCnt & (1u << 6)) != 0;

        const TInt bgMode = gPpu.dispCnt & 7;

        for (TInt i = 127; i >= 0; --i)
        {
            const TUint32 oamAddr =
                KOamBase + static_cast<TUint32>(i * 8);

            const TUint16 attr0 =
                NgbaMemoryRead16(oamAddr + 0u);

            const TUint16 attr1 =
                NgbaMemoryRead16(oamAddr + 2u);

            const TUint16 attr2 =
                NgbaMemoryRead16(oamAddr + 4u);

            const TInt affMode = (attr0 >> 8) & 3;

            if (affMode == 2)
                continue;

            const TInt shape = (attr0 >> 14) & 3;
            const TInt size = (attr1 >> 14) & 3;

            if (shape >= 3)
                continue;

            const TInt width = KObjWidthTable[shape][size];
            const TInt height = KObjHeightTable[shape][size];

            const TBool isAffine =
                (affMode == 1 || affMode == 3);

            const TBool doubleSize = (affMode == 3);

            const TInt boundW =
                doubleSize ? (width * 2) : width;

            const TInt boundH =
                doubleSize ? (height * 2) : height;

            TInt objY = attr0 & 0xFF;

            if (objY >= 160)
                objY -= 256;

            if (aY < objY || aY >= objY + boundH)
                continue;

            TInt objX = attr1 & 0x1FF;

            if (objX >= 240)
                objX -= 512;

            const TInt gfxMode = (attr0 >> 10) & 3;

            if (gfxMode == 3)
                continue;

            const TBool is8bpp =
                (attr0 & (1u << 13)) != 0;

            const TUint32 baseTile =
                attr2 & 0x03FFu;

            const TUint8 priority =
                static_cast<TUint8>((attr2 >> 10) & 3);

            const TUint32 palBank =
                (attr2 >> 12) & 0x0Fu;

            if (bgMode >= 3 && baseTile < 512u)
                continue;

            const TUint32 tilesPerRow = mapping1D
                ? static_cast<TUint32>(width >> 3) *
                  (is8bpp ? 2u : 1u)
                : 32u;

            if (isAffine)
            {
                const TUint32 affIdx =
                    (attr1 >> 9) & 0x1Fu;

                const TUint32 matBase =
                    KOamBase + affIdx * 32u;

                const TInt32 pa =
                    static_cast<TInt16>(
                        NgbaMemoryRead16(matBase + 0x06u)
                    );

                const TInt32 pb =
                    static_cast<TInt16>(
                        NgbaMemoryRead16(matBase + 0x0Eu)
                    );

                const TInt32 pc =
                    static_cast<TInt16>(
                        NgbaMemoryRead16(matBase + 0x16u)
                    );

                const TInt32 pd =
                    static_cast<TInt16>(
                        NgbaMemoryRead16(matBase + 0x1Eu)
                    );

                const TInt halfBoundW = boundW >> 1;
                const TInt halfBoundH = boundH >> 1;

                const TInt32 halfTexW =
                    static_cast<TInt32>(width) << 7;

                const TInt32 halfTexH =
                    static_cast<TInt32>(height) << 7;

                const TInt dy =
                    (aY - objY) - halfBoundH;

                TInt32 texX =
                    pa * (-halfBoundW) + pb * dy + halfTexW;

                TInt32 texY =
                    pc * (-halfBoundW) + pd * dy + halfTexH;

                for (TInt sx = 0; sx < boundW; ++sx)
                {
                    const TInt px = texX >> 8;
                    const TInt py = texY >> 8;

                    texX += pa;
                    texY += pc;

                    const TInt screenX = objX + sx;

                    if (screenX < 0 ||
                        screenX >= KGbaScreenWidth)
                    {
                        continue;
                    }

                    if (px < 0 || px >= width ||
                        py < 0 || py >= height)
                    {
                        continue;
                    }

                    const TInt tileX = px >> 3;
                    const TInt tileY = py >> 3;
                    const TInt fineX = px & 7;
                    const TInt fineY = py & 7;

                    TUint8 colorIdx = 0;
                    TUint16 rgb555 = 0;

                    if (is8bpp)
                    {
                        const TUint32 tileIdx =
                            baseTile +
                            static_cast<TUint32>(tileY) *
                            tilesPerRow +
                            static_cast<TUint32>(tileX * 2);

                        const TUint32 addr =
                            KVramBase + 0x10000u +
                            (tileIdx & 0x3FFu) * 32u +
                            static_cast<TUint32>(
                                fineY * 8 + fineX
                            );

                        colorIdx = NgbaMemoryRead8(addr);

                        if (colorIdx != 0)
                        {
                            rgb555 =
                                NgbaMemoryRead16(
                                    KPalRamBase + 0x200u +
                                    static_cast<TUint32>(
                                        colorIdx
                                    ) * 2u
                                ) & 0x7FFFu;
                        }
                    }
                    else
                    {
                        const TUint32 tileIdx =
                            baseTile +
                            static_cast<TUint32>(tileY) *
                            tilesPerRow +
                            static_cast<TUint32>(tileX);

                        const TUint32 addr =
                            KVramBase + 0x10000u +
                            (tileIdx & 0x3FFu) * 32u +
                            static_cast<TUint32>(
                                fineY * 4 + (fineX >> 1)
                            );

                        const TUint8 packed =
                            NgbaMemoryRead8(addr);

                        colorIdx = (fineX & 1)
                            ? static_cast<TUint8>(packed >> 4)
                            : static_cast<TUint8>(packed & 0x0Fu);

                        if (colorIdx != 0)
                        {
                            const TUint32 palIndex =
                                palBank * 16u + colorIdx;

                            rgb555 =
                                NgbaMemoryRead16(
                                    KPalRamBase + 0x200u +
                                    palIndex * 2u
                                ) & 0x7FFFu;
                        }
                    }

                    if (colorIdx != 0)
                    {
                        if (gfxMode == 2)
                        {
                            aObjWindow[screenX] = ETrue;
                        }
                        else if (priority <= aObjPrio[screenX])
                        {
                            aObjColor[screenX] = rgb555;
                            aObjPrio[screenX] = priority;

                            aObjSemiTrans[screenX] =
                                (gfxMode == 1);
                        }
                    }
                }
            }
            else
            {
                const TBool hFlip =
                    (attr1 & (1u << 12)) != 0;

                const TBool vFlip =
                    (attr1 & (1u << 13)) != 0;

                const TInt localY = vFlip
                    ? (height - 1 - (aY - objY))
                    : (aY - objY);

                const TInt tileY = localY >> 3;
                const TInt fineY = localY & 7;

                for (TInt sx = 0; sx < width; ++sx)
                {
                    const TInt screenX = objX + sx;

                    if (screenX < 0 ||
                        screenX >= KGbaScreenWidth)
                    {
                        continue;
                    }

                    const TInt localX = hFlip
                        ? (width - 1 - sx)
                        : sx;

                    const TInt tileX = localX >> 3;
                    const TInt fineX = localX & 7;

                    TUint8 colorIdx = 0;
                    TUint16 rgb555 = 0;

                    if (is8bpp)
                    {
                        const TUint32 tileIdx =
                            baseTile +
                            static_cast<TUint32>(tileY) *
                            tilesPerRow +
                            static_cast<TUint32>(tileX * 2);

                        const TUint32 addr =
                            KVramBase + 0x10000u +
                            (tileIdx & 0x3FFu) * 32u +
                            static_cast<TUint32>(
                                fineY * 8 + fineX
                            );

                        colorIdx = NgbaMemoryRead8(addr);

                        if (colorIdx != 0)
                        {
                            rgb555 =
                                NgbaMemoryRead16(
                                    KPalRamBase + 0x200u +
                                    static_cast<TUint32>(
                                        colorIdx
                                    ) * 2u
                                ) & 0x7FFFu;
                        }
                    }
                    else
                    {
                        const TUint32 tileIdx =
                            baseTile +
                            static_cast<TUint32>(tileY) *
                            tilesPerRow +
                            static_cast<TUint32>(tileX);

                        const TUint32 addr =
                            KVramBase + 0x10000u +
                            (tileIdx & 0x3FFu) * 32u +
                            static_cast<TUint32>(
                                fineY * 4 + (fineX >> 1)
                            );

                        const TUint8 packed =
                            NgbaMemoryRead8(addr);

                        colorIdx = (fineX & 1)
                            ? static_cast<TUint8>(packed >> 4)
                            : static_cast<TUint8>(packed & 0x0Fu);

                        if (colorIdx != 0)
                        {
                            const TUint32 palIndex =
                                palBank * 16u + colorIdx;

                            rgb555 =
                                NgbaMemoryRead16(
                                    KPalRamBase + 0x200u +
                                    palIndex * 2u
                                ) & 0x7FFFu;
                        }
                    }

                    if (colorIdx != 0)
                    {
                        if (gfxMode == 2)
                        {
                            aObjWindow[screenX] = ETrue;
                        }
                        else if (priority <= aObjPrio[screenX])
                        {
                            aObjColor[screenX] = rgb555;
                            aObjPrio[screenX] = priority;

                            aObjSemiTrans[screenX] =
                                (gfxMode == 1);
                        }
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------
    // WINDOWING & COLOR BLENDING HELPERS
    // ---------------------------------------------------------

    TBool IsInsideWindow(
        TInt aX,
        TInt aY,
        TUint16 aWinH,
        TUint16 aWinV
    )
    {
        const TInt x1 = (aWinH >> 8) & 0xFF;
        const TInt x2 = aWinH & 0xFF;

        const TInt y1 = (aWinV >> 8) & 0xFF;
        const TInt y2 = aWinV & 0xFF;

        TBool inY = EFalse;

        if (y1 <= y2)
            inY = (aY >= y1 && aY < y2);
        else
            inY = (aY >= y1 || aY < y2);

        if (!inY)
            return EFalse;

        if (x1 <= x2)
            return (aX >= x1 && aX < x2);

        return (aX >= x1 || aX < x2);
    }

    TUint8 GetWindowMaskForPixel(
        TInt aX,
        TInt aY,
        TBool aObjWindowPixel
    )
    {
        const TBool win0En =
            (gPpu.dispCnt & (1u << 13)) != 0;

        const TBool win1En =
            (gPpu.dispCnt & (1u << 14)) != 0;

        const TBool objWinEn =
            (gPpu.dispCnt & (1u << 15)) != 0;

        if (!win0En && !win1En && !objWinEn)
            return 0x3Fu;

        if (win0En &&
            IsInsideWindow(
                aX, aY,
                gPpu.win0H,
                gPpu.win0V
            ))
        {
            return static_cast<TUint8>(
                gPpu.winIn & 0x3Fu
            );
        }

        if (win1En &&
            IsInsideWindow(
                aX, aY,
                gPpu.win1H,
                gPpu.win1V
            ))
        {
            return static_cast<TUint8>(
                (gPpu.winIn >> 8) & 0x3Fu
            );
        }

        if (objWinEn && aObjWindowPixel)
        {
            return static_cast<TUint8>(
                (gPpu.winOut >> 8) & 0x3Fu
            );
        }

        return static_cast<TUint8>(
            gPpu.winOut & 0x3Fu
        );
    }

    TUint16 BlendAlpha(TUint16 aTop, TUint16 aBottom)
    {
        TInt eva = gPpu.bldAlpha & 0x1F;
        TInt evb = (gPpu.bldAlpha >> 8) & 0x1F;

        if (eva > 16) eva = 16;
        if (evb > 16) evb = 16;

        const TInt r1 = aTop & 0x1F;
        const TInt g1 = (aTop >> 5) & 0x1F;
        const TInt b1 = (aTop >> 10) & 0x1F;

        const TInt r2 = aBottom & 0x1F;
        const TInt g2 = (aBottom >> 5) & 0x1F;
        const TInt b2 = (aBottom >> 10) & 0x1F;

        const TInt r =
            ClampInt((r1 * eva + r2 * evb) >> 4, 0, 31);

        const TInt g =
            ClampInt((g1 * eva + g2 * evb) >> 4, 0, 31);

        const TInt b =
            ClampInt((b1 * eva + b2 * evb) >> 4, 0, 31);

        return static_cast<TUint16>(
            r | (g << 5) | (b << 10)
        );
    }

    TUint16 BlendBrightnessIncrease(TUint16 aColor)
    {
        TInt evy = gPpu.bldY & 0x1F;

        if (evy > 16)
            evy = 16;

        TInt r = aColor & 0x1F;
        TInt g = (aColor >> 5) & 0x1F;
        TInt b = (aColor >> 10) & 0x1F;

        r += ((31 - r) * evy) >> 4;
        g += ((31 - g) * evy) >> 4;
        b += ((31 - b) * evy) >> 4;

        return static_cast<TUint16>(
            r | (g << 5) | (b << 10)
        );
    }

    TUint16 BlendBrightnessDecrease(TUint16 aColor)
    {
        TInt evy = gPpu.bldY & 0x1F;

        if (evy > 16)
            evy = 16;

        TInt r = aColor & 0x1F;
        TInt g = (aColor >> 5) & 0x1F;
        TInt b = (aColor >> 10) & 0x1F;

        r -= (r * evy) >> 4;
        g -= (g * evy) >> 4;
        b -= (b * evy) >> 4;

        return static_cast<TUint16>(
            r | (g << 5) | (b << 10)
        );
    }

    // ---------------------------------------------------------
    // KOMPOSISI SCANLINE LENGKAP
    // ---------------------------------------------------------

    void RenderScanlineInternal(TInt aY)
    {
        TUint16* dstRow =
            &gPpu.frameBuffer[aY * KGbaScreenWidth];

        // Forced Blank (DISPCNT bit 7): layar putih penuh.
        if (gPpu.dispCnt & (1u << 7))
        {
            for (TInt x = 0; x < KGbaScreenWidth; ++x)
                dstRow[x] = 0x7FFFu;

            return;
        }

        const TInt bgMode = gPpu.dispCnt & 7;

        TUint16 bgLine[4][KGbaScreenWidth];
        TUint16 objColor[KGbaScreenWidth];
        TUint8 objPrio[KGbaScreenWidth];
        TBool objSemiTrans[KGbaScreenWidth];
        TBool objWindow[KGbaScreenWidth];

        for (TInt x = 0; x < KGbaScreenWidth; ++x)
        {
            bgLine[0][x] = KTransparent;
            bgLine[1][x] = KTransparent;
            bgLine[2][x] = KTransparent;
            bgLine[3][x] = KTransparent;

            objColor[x] = KTransparent;
            objPrio[x] = 4;
            objSemiTrans[x] = EFalse;
            objWindow[x] = EFalse;
        }

        if (bgMode == 0)
        {
            for (TInt bg = 0; bg < 4; ++bg)
            {
                if (gPpu.dispCnt & (1u << (8 + bg)))
                {
                    RenderTiledBgScanline(
                        bg,
                        aY,
                        bgLine[bg]
                    );
                }
            }
        }
        else if (bgMode == 1)
        {
            if (gPpu.dispCnt & (1u << 8))
                RenderTiledBgScanline(0, aY, bgLine[0]);

            if (gPpu.dispCnt & (1u << 9))
                RenderTiledBgScanline(1, aY, bgLine[1]);

            if (gPpu.dispCnt & (1u << 10))
                RenderAffineBgScanline(2, bgLine[2]);
        }
        else if (bgMode == 2)
        {
            if (gPpu.dispCnt & (1u << 10))
                RenderAffineBgScanline(2, bgLine[2]);

            if (gPpu.dispCnt & (1u << 11))
                RenderAffineBgScanline(3, bgLine[3]);
        }
        else if (bgMode == 3)
        {
            if (gPpu.dispCnt & (1u << 10))
            {
                const TUint32 rowAddr =
                    KVramBase +
                    static_cast<TUint32>(
                        aY * KGbaScreenWidth * 2
                    );

                for (TInt x = 0; x < KGbaScreenWidth; ++x)
                {
                    bgLine[2][x] =
                        NgbaMemoryRead16(
                            rowAddr +
                            static_cast<TUint32>(x * 2)
                        ) & 0x7FFFu;
                }
            }
        }
        else if (bgMode == 4)
        {
            if (gPpu.dispCnt & (1u << 10))
            {
                const TUint32 frameBase =
                    (gPpu.dispCnt & (1u << 4))
                    ? (KVramBase + 0xA000u)
                    : KVramBase;

                const TUint32 rowAddr =
                    frameBase +
                    static_cast<TUint32>(
                        aY * KGbaScreenWidth
                    );

                for (TInt x = 0; x < KGbaScreenWidth; ++x)
                {
                    const TUint8 idx =
                        NgbaMemoryRead8(
                            rowAddr +
                            static_cast<TUint32>(x)
                        );

                    if (idx != 0)
                    {
                        bgLine[2][x] =
                            NgbaMemoryRead16(
                                KPalRamBase +
                                static_cast<TUint32>(idx) * 2u
                            ) & 0x7FFFu;
                    }
                }
            }
        }
        else if (bgMode == 5)
        {
            if ((gPpu.dispCnt & (1u << 10)) != 0 &&
                aY < 128)
            {
                const TUint32 frameBase =
                    (gPpu.dispCnt & (1u << 4))
                    ? (KVramBase + 0xA000u)
                    : KVramBase;

                const TUint32 rowAddr =
                    frameBase +
                    static_cast<TUint32>(aY * 160 * 2);

                for (TInt x = 0; x < 160; ++x)
                {
                    bgLine[2][x] =
                        NgbaMemoryRead16(
                            rowAddr +
                            static_cast<TUint32>(x * 2)
                        ) & 0x7FFFu;
                }
            }
        }

        RenderSpritesScanline(
            aY,
            objColor,
            objPrio,
            objSemiTrans,
            objWindow
        );

        const TUint16 backdrop =
            NgbaMemoryRead16(KPalRamBase) & 0x7FFFu;

        const TInt bldMode =
            (gPpu.bldCnt >> 6) & 3;

        TInt sortedBg[4];
        TInt sortedBgCount = 0;

        for (TInt prio = 0; prio < 4; ++prio)
        {
            for (TInt bg = 0; bg < 4; ++bg)
            {
                if ((gPpu.dispCnt & (1u << (8 + bg))) == 0)
                    continue;

                if ((gPpu.bgCnt[bg] & 3) == prio)
                    sortedBg[sortedBgCount++] = bg;
            }
        }

        for (TInt x = 0; x < KGbaScreenWidth; ++x)
        {
            const TUint8 winMask =
                GetWindowMaskForPixel(
                    x,
                    aY,
                    objWindow[x]
                );

            const TBool colorEffectAllowed =
                (winMask & (1u << 5)) != 0;

            TUint16 topColor = backdrop;
            TInt topLayer = KLayerBackdrop;

            TUint16 botColor = backdrop;
            TInt botLayer = KLayerBackdrop;

            TInt foundCount = 0;

            const TBool hasObj =
                ((winMask & (1u << 4)) != 0) &&
                (objColor[x] != KTransparent);

            const TInt objPr = objPrio[x];

            TBool objInserted = !hasObj;

            for (TInt idx = 0;
                 idx < sortedBgCount && foundCount < 2;
                 ++idx)
            {
                const TInt bg = sortedBg[idx];
                const TInt bgPr = gPpu.bgCnt[bg] & 3;

                if (!objInserted && objPr <= bgPr)
                {
                    if (foundCount == 0)
                    {
                        topColor = objColor[x];
                        topLayer = KLayerObj;
                    }
                    else
                    {
                        botColor = objColor[x];
                        botLayer = KLayerObj;
                    }

                    ++foundCount;
                    objInserted = ETrue;

                    if (foundCount >= 2)
                        break;
                }

                if ((winMask & (1u << bg)) != 0 &&
                    bgLine[bg][x] != KTransparent)
                {
                    if (foundCount == 0)
                    {
                        topColor = bgLine[bg][x];
                        topLayer = bg;
                    }
                    else
                    {
                        botColor = bgLine[bg][x];
                        botLayer = bg;
                    }

                    ++foundCount;
                }
            }

            if (!objInserted && foundCount < 2)
            {
                if (foundCount == 0)
                {
                    topColor = objColor[x];
                    topLayer = KLayerObj;
                }
                else
                {
                    botColor = objColor[x];
                    botLayer = KLayerObj;
                }

                ++foundCount;
            }

            TUint16 finalColor = topColor;

            const TBool isSemiTransObj =
                (topLayer == KLayerObj) &&
                objSemiTrans[x];

            const TBool target1Match =
                (gPpu.bldCnt & (1u << topLayer)) != 0;

            const TBool target2Match =
                (gPpu.bldCnt & (1u << (8 + botLayer))) != 0;

            if (isSemiTransObj && target2Match)
            {
                finalColor =
                    BlendAlpha(topColor, botColor);
            }
            else if (colorEffectAllowed && target1Match)
            {
                if (bldMode == 1 && target2Match)
                {
                    finalColor =
                        BlendAlpha(topColor, botColor);
                }
                else if (bldMode == 2)
                {
                    finalColor =
                        BlendBrightnessIncrease(topColor);
                }
                else if (bldMode == 3)
                {
                    finalColor =
                        BlendBrightnessDecrease(topColor);
                }
            }

            dstRow[x] = finalColor;
        }
    }

    void UpdateVCountAndStat(
        TUint16 aLine,
        TBool aHBlank
    )
    {
        gPpu.vCount = aLine;

        TUint16 stat = gPpu.dispStat & 0xFFF8u;

        if (aLine >= 160u && aLine < 227u)
            stat |= 0x0001u;

        if (aHBlank)
            stat |= 0x0002u;

        const TUint16 lyc = (stat >> 8) & 0xFFu;

        if (aLine == lyc)
            stat |= 0x0004u;

        gPpu.dispStat = stat;

        NgbaMemoryWrite16(KRegDispStat, stat);
        NgbaMemoryWrite16(KRegVCount, aLine);
    }
}

// ---------------------------------------------------------
// RESET & CALLBACKS
// ---------------------------------------------------------

void NgbaPpuReset()
{
    gPpuFrameCounter = 0;
    gLastLoggedDispCnt = 0xFFFFu;

    gPpu.dispCnt = 0;
    gPpu.greenSwap = 0;
    gPpu.dispStat = 0;
    gPpu.vCount = 0;

    for (TInt i = 0; i < 4; ++i)
    {
        gPpu.bgCnt[i] = 0;
        gPpu.bgHOfs[i] = 0;
        gPpu.bgVOfs[i] = 0;
    }

    for (TInt i = 0; i < 2; ++i)
    {
        gPpu.bgPa[i] = 0x0100;
        gPpu.bgPb[i] = 0;
        gPpu.bgPc[i] = 0;
        gPpu.bgPd[i] = 0x0100;

        gPpu.bgAffX[i] = 0;
        gPpu.bgAffY[i] = 0;

        gPpu.internalX[i] = 0;
        gPpu.internalY[i] = 0;
    }

    gPpu.win0H = 0;
    gPpu.win1H = 0;
    gPpu.win0V = 0;
    gPpu.win1V = 0;

    gPpu.winIn = 0;
    gPpu.winOut = 0;

    gPpu.mosaic = 0;
    gPpu.bldCnt = 0;
    gPpu.bldAlpha = 0;
    gPpu.bldY = 0;

    for (TInt i = 0; i < KGbaScreenPixels; ++i)
        gPpu.frameBuffer[i] = 0;

    WritePpuLog(_L("PPU RESET"));
    LogPpuDispCnt(gPpu.dispCnt);
}

void NgbaPpuSetIrqCallback(
    TNgbaPpuIrqCallback aCallback
)
{
    gIrqCallback = aCallback;
}

void NgbaPpuSetVBlankCallback(
    TNgbaPpuEventCallback aCallback
)
{
    gVBlankCallback = aCallback;
}

void NgbaPpuSetHBlankCallback(
    TNgbaPpuEventCallback aCallback
)
{
    gHBlankCallback = aCallback;
}

// ---------------------------------------------------------
// REGISTER I/O READ & WRITE
// ---------------------------------------------------------

TUint8 NgbaPpuReadIo8(TUint32 aAddress)
{
    const TUint16 val16 =
        NgbaPpuReadIo16(aAddress & ~1u);

    return (aAddress & 1u)
        ? static_cast<TUint8>(val16 >> 8)
        : static_cast<TUint8>(val16 & 0xFFu);
}

TUint16 NgbaPpuReadIo16(TUint32 aAddress)
{
    switch (aAddress & ~1u)
    {
    case KRegDispCnt:
        return gPpu.dispCnt;

    case KRegGreenSwap:
        return gPpu.greenSwap;

    case KRegDispStat:
        return gPpu.dispStat;

    case KRegVCount:
        return gPpu.vCount;

    case 0x04000008u:
        return gPpu.bgCnt[0];

    case 0x0400000Au:
        return gPpu.bgCnt[1];

    case 0x0400000Cu:
        return gPpu.bgCnt[2];

    case 0x0400000Eu:
        return gPpu.bgCnt[3];

    case 0x04000048u:
        return gPpu.winIn;

    case 0x0400004Au:
        return gPpu.winOut;

    case 0x04000050u:
        return gPpu.bldCnt;

    case 0x04000052u:
        return gPpu.bldAlpha;

    default:
        return 0;
    }
}

TUint32 NgbaPpuReadIo32(TUint32 aAddress)
{
    const TUint32 aligned = aAddress & ~3u;

    const TUint32 lo =
        NgbaPpuReadIo16(aligned);

    const TUint32 hi =
        NgbaPpuReadIo16(aligned + 2u);

    return lo | (hi << 16);
}

void NgbaPpuWriteIo8(
    TUint32 aAddress,
    TUint8 aValue
)
{
    const TUint32 aligned = aAddress & ~1u;

    TUint16 current =
        NgbaPpuReadIo16(aligned);

    if (aAddress & 1u)
    {
        current = static_cast<TUint16>(
            (current & 0x00FFu) |
            (static_cast<TUint16>(aValue) << 8)
        );
    }
    else
    {
        current = static_cast<TUint16>(
            (current & 0xFF00u) | aValue
        );
    }

    NgbaPpuWriteIo16(aligned, current);
}

void NgbaPpuWriteIo16(
    TUint32 aAddress,
    TUint16 aValue
)
{
    switch (aAddress & ~1u)
    {
    case KRegDispCnt:
        gPpu.dispCnt = aValue;

        if (aValue != gLastLoggedDispCnt)
        {
            LogPpuDispCnt(aValue);
            gLastLoggedDispCnt = aValue;
        }
        break;

    case KRegGreenSwap:
        gPpu.greenSwap = aValue;
        break;

    case KRegDispStat:
        gPpu.dispStat =
            (gPpu.dispStat & 0x0007u) |
            (aValue & 0xFFF8u);
        break;

    case 0x04000008u:
        gPpu.bgCnt[0] = aValue;
        break;

    case 0x0400000Au:
        gPpu.bgCnt[1] = aValue;
        break;

    case 0x0400000Cu:
        gPpu.bgCnt[2] = aValue;
        break;

    case 0x0400000Eu:
        gPpu.bgCnt[3] = aValue;
        break;

    case 0x04000010u:
        gPpu.bgHOfs[0] = aValue & 0x1FFu;
        break;

    case 0x04000012u:
        gPpu.bgVOfs[0] = aValue & 0x1FFu;
        break;

    case 0x04000014u:
        gPpu.bgHOfs[1] = aValue & 0x1FFu;
        break;

    case 0x04000016u:
        gPpu.bgVOfs[1] = aValue & 0x1FFu;
        break;

    case 0x04000018u:
        gPpu.bgHOfs[2] = aValue & 0x1FFu;
        break;

    case 0x0400001Au:
        gPpu.bgVOfs[2] = aValue & 0x1FFu;
        break;

    case 0x0400001Cu:
        gPpu.bgHOfs[3] = aValue & 0x1FFu;
        break;

    case 0x0400001Eu:
        gPpu.bgVOfs[3] = aValue & 0x1FFu;
        break;

    // Register Affine BG2.
    case 0x04000020u:
        gPpu.bgPa[0] = static_cast<TInt16>(aValue);
        break;

    case 0x04000022u:
        gPpu.bgPb[0] = static_cast<TInt16>(aValue);
        break;

    case 0x04000024u:
        gPpu.bgPc[0] = static_cast<TInt16>(aValue);
        break;

    case 0x04000026u:
        gPpu.bgPd[0] = static_cast<TInt16>(aValue);
        break;

    case 0x04000028u:
        gPpu.bgAffX[0] = SignExtend28(
            (static_cast<TUint32>(gPpu.bgAffX[0]) &
             0xFFFF0000u) | aValue
        );

        gPpu.internalX[0] = gPpu.bgAffX[0];
        break;

    case 0x0400002Au:
        gPpu.bgAffX[0] = SignExtend28(
            (static_cast<TUint32>(gPpu.bgAffX[0]) &
             0x0000FFFFu) |
            (static_cast<TUint32>(aValue) << 16)
        );

        gPpu.internalX[0] = gPpu.bgAffX[0];
        break;

    case 0x0400002Cu:
        gPpu.bgAffY[0] = SignExtend28(
            (static_cast<TUint32>(gPpu.bgAffY[0]) &
             0xFFFF0000u) | aValue
        );

        gPpu.internalY[0] = gPpu.bgAffY[0];
        break;

    case 0x0400002Eu:
        gPpu.bgAffY[0] = SignExtend28(
            (static_cast<TUint32>(gPpu.bgAffY[0]) &
             0x0000FFFFu) |
            (static_cast<TUint32>(aValue) << 16)
        );

        gPpu.internalY[0] = gPpu.bgAffY[0];
        break;

    // Register Affine BG3.
    case 0x04000030u:
        gPpu.bgPa[1] = static_cast<TInt16>(aValue);
        break;

    case 0x04000032u:
        gPpu.bgPb[1] = static_cast<TInt16>(aValue);
        break;

    case 0x04000034u:
        gPpu.bgPc[1] = static_cast<TInt16>(aValue);
        break;

    case 0x04000036u:
        gPpu.bgPd[1] = static_cast<TInt16>(aValue);
        break;

    case 0x04000038u:
        gPpu.bgAffX[1] = SignExtend28(
            (static_cast<TUint32>(gPpu.bgAffX[1]) &
             0xFFFF0000u) | aValue
        );

        gPpu.internalX[1] = gPpu.bgAffX[1];
        break;

    case 0x0400003Au:
        gPpu.bgAffX[1] = SignExtend28(
            (static_cast<TUint32>(gPpu.bgAffX[1]) &
             0x0000FFFFu) |
            (static_cast<TUint32>(aValue) << 16)
        );

        gPpu.internalX[1] = gPpu.bgAffX[1];
        break;

    case 0x0400003Cu:
        gPpu.bgAffY[1] = SignExtend28(
            (static_cast<TUint32>(gPpu.bgAffY[1]) &
             0xFFFF0000u) | aValue
        );

        gPpu.internalY[1] = gPpu.bgAffY[1];
        break;

    case 0x0400003Eu:
        gPpu.bgAffY[1] = SignExtend28(
            (static_cast<TUint32>(gPpu.bgAffY[1]) &
             0x0000FFFFu) |
            (static_cast<TUint32>(aValue) << 16)
        );

        gPpu.internalY[1] = gPpu.bgAffY[1];
        break;

    // Window & Blending Registers.
    case 0x04000040u:
        gPpu.win0H = aValue;
        break;

    case 0x04000042u:
        gPpu.win1H = aValue;
        break;

    case 0x04000044u:
        gPpu.win0V = aValue;
        break;

    case 0x04000046u:
        gPpu.win1V = aValue;
        break;

    case 0x04000048u:
        gPpu.winIn = aValue & 0x3F3Fu;
        break;

    case 0x0400004Au:
        gPpu.winOut = aValue & 0x3F3Fu;
        break;

    case 0x0400004Cu:
        gPpu.mosaic = aValue;
        break;

    case 0x04000050u:
        gPpu.bldCnt = aValue & 0x3FFFu;
        break;

    case 0x04000052u:
        gPpu.bldAlpha = aValue & 0x1F1Fu;
        break;

    case 0x04000054u:
        gPpu.bldY = aValue & 0x001Fu;
        break;

    default:
        break;
    }
}

void NgbaPpuWriteIo32(
    TUint32 aAddress,
    TUint32 aValue
)
{
    const TUint32 aligned = aAddress & ~3u;

    NgbaPpuWriteIo16(
        aligned,
        static_cast<TUint16>(aValue & 0xFFFFu)
    );

    NgbaPpuWriteIo16(
        aligned + 2u,
        static_cast<TUint16>(aValue >> 16)
    );
}

// ---------------------------------------------------------
// FRAME & SCANLINE EXECUTION
// ---------------------------------------------------------

void NgbaPpuRenderLine(TInt aLine)
{
    if (aLine < 0 || aLine >= KVisibleLines)
        return;

    if (aLine == 0)
    {
        gPpu.internalX[0] = gPpu.bgAffX[0];
        gPpu.internalY[0] = gPpu.bgAffY[0];

        gPpu.internalX[1] = gPpu.bgAffX[1];
        gPpu.internalY[1] = gPpu.bgAffY[1];
    }

    RenderScanlineInternal(aLine);

    gPpu.internalX[0] += gPpu.bgPb[0];
    gPpu.internalY[0] += gPpu.bgPd[0];

    gPpu.internalX[1] += gPpu.bgPb[1];
    gPpu.internalY[1] += gPpu.bgPd[1];
}

void NgbaPpuRunFrame(TNgbaCpuState& aCpu)
{
    gPpu.internalX[0] = gPpu.bgAffX[0];
    gPpu.internalY[0] = gPpu.bgAffY[0];

    gPpu.internalX[1] = gPpu.bgAffX[1];
    gPpu.internalY[1] = gPpu.bgAffY[1];

    for (TUint16 line = 0; line < KTotalLines; ++line)
    {
        // 1. Fase HDraw.
        UpdateVCountAndStat(line, EFalse);

        if ((gPpu.dispStat & 0x0004u) != 0 &&
            (gPpu.dispStat & (1u << 5)) != 0)
        {
            RaisePpuIrq(KIrqVCount);
        }

        if (line == KVisibleLines)
        {
            if (gPpu.dispStat & (1u << 3))
                RaisePpuIrq(KIrqVBlank);

            if (gVBlankCallback != NULL)
                gVBlankCallback();
        }

        RunCpuCycles(aCpu, KCyclesHDraw);

        // 2. Fase HBlank.
        UpdateVCountAndStat(line, ETrue);

        if (line < KVisibleLines)
        {
            RenderScanlineInternal(line);

            gPpu.internalX[0] += gPpu.bgPb[0];
            gPpu.internalY[0] += gPpu.bgPd[0];

            gPpu.internalX[1] += gPpu.bgPb[1];
            gPpu.internalY[1] += gPpu.bgPd[1];

            if (gHBlankCallback != NULL)
                gHBlankCallback();
        }

        if (gPpu.dispStat & (1u << 4))
            RaisePpuIrq(KIrqHBlank);

        RunCpuCycles(aCpu, KCyclesHBlank);
    }

    // -----------------------------------------------------
    // PPU FRAME LOG
    // -----------------------------------------------------

    ++gPpuFrameCounter;

    if (gPpuFrameCounter == 1u ||
        (gPpuFrameCounter % 300u) == 0u)
    {
        TBuf<128> message;

        message.Copy(_L("FRAME="));

        message.AppendNum(
            static_cast<TInt>(gPpuFrameCounter)
        );

        message.Append(_L(" DISPCNT=0x"));

        message.AppendNum(
            static_cast<TInt>(gPpu.dispCnt),
            EHex
        );

        message.Append(_L(" VCOUNT="));

        message.AppendNum(
            static_cast<TInt>(gPpu.vCount)
        );

        message.Append(_L(" CPU_CYCLES=0x"));

        message.AppendNum(
            static_cast<TInt>(aCpu.cycles),
            EHex
        );

        message.Append(_L(" PIXEL0=0x"));

        message.AppendNum(
            static_cast<TInt>(gPpu.frameBuffer[0]),
            EHex
        );

        WritePpuLog(message);
    }
}

// ---------------------------------------------------------
// FRAMEBUFFER ACCESS & COLOR CONVERSION
// ---------------------------------------------------------

const TUint16* NgbaPpuGetFrameBufferRgb555()
{
    return gPpu.frameBuffer;
}

void NgbaPpuConvertToRgb565(TUint16* aDestRgb565)
{
    for (TInt i = 0; i < KGbaScreenPixels; ++i)
    {
        const TUint16 c = gPpu.frameBuffer[i];

        const TUint16 r =
            static_cast<TUint16>(
                (c & 0x001Fu) << 11
            );

        // Perbaikan ekstraksi bit hijau RGB555 ke RGB565.
        const TUint16 g =
            static_cast<TUint16>(
                ((c & 0x03E0u) << 1) |
                ((c >> 4) & 0x0020u)
            );

        const TUint16 b =
            static_cast<TUint16>(
                (c >> 10) & 0x001Fu
            );

        aDestRgb565[i] =
            static_cast<TUint16>(r | g | b);
    }
}

void NgbaPpuConvertToXrgb8888(TUint32* aDestXrgb8888)
{
    for (TInt i = 0; i < KGbaScreenPixels; ++i)
    {
        const TUint32 c = gPpu.frameBuffer[i];

        const TUint32 r5 = c & 0x1Fu;
        const TUint32 g5 = (c >> 5) & 0x1Fu;
        const TUint32 b5 = (c >> 10) & 0x1Fu;

        const TUint32 r8 =
            (r5 << 3) | (r5 >> 2);

        const TUint32 g8 =
            (g5 << 3) | (g5 >> 2);

        const TUint32 b8 =
            (b5 << 3) | (b5 >> 2);

        aDestXrgb8888[i] =
            0xFF000000u |
            (r8 << 16) |
            (g8 << 8) |
            b8;
    }
}

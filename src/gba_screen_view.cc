// gba_screen_view.cc
#include "gba_screen_view.h"
#include "gba_ppu.h"
#include "gba_memory.h"
#include <eikenv.h>
#include <e32keys.h>

namespace
{
    const TInt KFrameIntervalMicros = 16667; // 60 FPS penuh pada ARM11 680 MHz

    // Skala integer 2x dari 240x160 -> 480x320 (presisi piksel di layar 640x480 Nokia E6).
    const TInt KScaledWidth  = KGbaScreenWidth * 2;
    const TInt KScaledHeight = KGbaScreenHeight * 2;

    // Bitmask tombol GBA (register KEYINPUT 0x04000130).
    const TUint16 KGbaKeyA      = 0x0001u;
    const TUint16 KGbaKeyB      = 0x0002u;
    const TUint16 KGbaKeySelect = 0x0004u;
    const TUint16 KGbaKeyStart  = 0x0008u;
    const TUint16 KGbaKeyRight  = 0x0010u;
    const TUint16 KGbaKeyLeft   = 0x0020u;
    const TUint16 KGbaKeyUp     = 0x0040u;
    const TUint16 KGbaKeyDown   = 0x0080u;
    const TUint16 KGbaKeyR      = 0x0100u;
    const TUint16 KGbaKeyL      = 0x0200u;

    TUint16 MapScanCodeToGbaBit(TInt aScanCode)
    {
        switch (aScanCode)
        {
        // D-Pad fisik Nokia E6 & tombol arah QWERTY (WASD)
        case EStdKeyUpArrow:
        case 'W':
            return KGbaKeyUp;

        case EStdKeyDownArrow:
        case 'S':
            return KGbaKeyDown;

        case EStdKeyLeftArrow:
        case 'A':
            return KGbaKeyLeft;

        case EStdKeyRightArrow:
        case 'D':
            return KGbaKeyRight;

        // Tombol A & B pada sisi kanan QWERTY Nokia E6 (L / K atau tengah D-Pad / Backspace)
        case 'L':
        case 'P':
        case EStdKeyDevice3:
            return KGbaKeyA;

        case 'K':
        case 'O':
        case EStdKeyBackspace:
            return KGbaKeyB;

        // Trigger L & R pada baris atas QWERTY (Q / E atau U / I)
        case 'Q':
        case 'U':
            return KGbaKeyL;

        case 'E':
        case 'I':
            return KGbaKeyR;

        // Start & Select
        case EStdKeyEnter:
        case 'M':
            return KGbaKeyStart;

        case EStdKeySpace:
        case 'N':
            return KGbaKeySelect;

        default:
            return 0;
        }
    }
}

CGbaScreenView* CGbaScreenView::NewL(
    const TRect& aRect,
    TNgbaCpuState& aCpu
)
{
    CGbaScreenView* self = new (ELeave) CGbaScreenView(aCpu);
    CleanupStack::PushL(self);
    self->ConstructL(aRect);
    CleanupStack::Pop(self);
    return self;
}

CGbaScreenView::CGbaScreenView(TNgbaCpuState& aCpu)
    : iCpu(aCpu),
      iDsa(NULL),
      iFrameBitmap(NULL),
      iTimer(NULL),
      iRgb555To888Lut(NULL),
      iKeyInput(0x03FFu),
      iDsaActive(EFalse)
{
}

CGbaScreenView::~CGbaScreenView()
{
    StopEmulation();
    delete iTimer;
    delete iDsa;
    delete iFrameBitmap;
    delete[] iRgb555To888Lut;
}

void CGbaScreenView::ConstructL(const TRect& aRect)
{
    CreateWindowL();
    SetRect(aRect);

    // Alokasi LUT 32.768 entri (128 KB) untuk konversi instan RGB555 -> XRGB8888.
    iRgb555To888Lut = new (ELeave) TUint32[32768];
    BuildColorLut();

    // Format native layar Symbian Anna (EColor16MU = 32-bit XRGB8888) agar BitBlt tanpa konversi.
    iFrameBitmap = new (ELeave) CFbsBitmap();
    User::LeaveIfError(
        iFrameBitmap->Create(
            TSize(KScaledWidth, KScaledHeight),
            EColor16MU
        )
    );

    iDsa = CDirectScreenAccess::NewL(
        iCoeEnv->WsSession(),
        *(iCoeEnv->ScreenDevice()),
        Window(),
        *this
    );

    iTimer = CPeriodic::NewL(CActive::EPriorityStandard);
    ActivateL();
}

void CGbaScreenView::BuildColorLut()
{
    for (TUint32 c = 0; c < 32768u; ++c)
    {
        const TUint32 r5 = c & 0x1Fu;
        const TUint32 g5 = (c >> 5) & 0x1Fu;
        const TUint32 b5 = (c >> 10) & 0x1Fu;

        const TUint32 r8 = (r5 << 3) | (r5 >> 2);
        const TUint32 g8 = (g5 << 3) | (g5 >> 2);
        const TUint32 b8 = (b5 << 3) | (b5 >> 2);

        iRgb555To888Lut[c] = (r8 << 16) | (g8 << 8) | b8;
    }
}

void CGbaScreenView::StartEmulation()
{
    if (!iDsaActive)
    {
        TRAPD(err, iDsa->StartL());
        if (err == KErrNone)
        {
            iDsaActive = ETrue;
            CFbsBitGc* gc = iDsa->Gc();
            gc->SetBrushStyle(CGraphicsContext::ESolidBrush);
            gc->SetBrushColor(KRgbBlack);
            gc->Clear(Rect());
        }
    }

    if (!iTimer->IsActive())
    {
        iTimer->Start(
            TTimeIntervalMicroSeconds32(KFrameIntervalMicros),
            TTimeIntervalMicroSeconds32(KFrameIntervalMicros),
            TCallBack(TickCallback, this)
        );
    }
}

void CGbaScreenView::StopEmulation()
{
    if (iTimer && iTimer->IsActive())
        iTimer->Cancel();

    if (iDsa && iDsaActive)
    {
        iDsa->Cancel();
        iDsaActive = EFalse;
    }
}

TUint16 CGbaScreenView::KeyInputState() const
{
    return iKeyInput;
}

void CGbaScreenView::Draw(const TRect& aRect) const
{
    CWindowGc& gc = SystemGc();
    gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
    gc.SetBrushColor(KRgbBlack);
    gc.Clear(aRect);
}

TKeyResponse CGbaScreenView::OfferKeyEventL(
    const TKeyEvent& aKeyEvent,
    TEventCode aType
)
{
    const TUint16 bit = MapScanCodeToGbaBit(aKeyEvent.iScanCode);
    if (bit == 0)
        return EKeyWasNotConsumed;

    if (aType == EEventKeyDown)
        iKeyInput &= static_cast<TUint16>(~bit);
    else if (aType == EEventKeyUp)
        iKeyInput |= bit;

    return EKeyWasConsumed;
}

void CGbaScreenView::HandleResourceChange(TInt aType)
{
    CCoeControl::HandleResourceChange(aType);
    if (aType == KEikDynamicLayoutVariantSwitch)
        SetExtentToWholeScreen();
}

void CGbaScreenView::Restart(
    RDirectScreenAccess::TTerminationReasons /*aReason*/
)
{
    TRAPD(err, iDsa->StartL());
    if (err == KErrNone)
    {
        iDsaActive = ETrue;
        CFbsBitGc* gc = iDsa->Gc();
        gc->SetBrushStyle(CGraphicsContext::ESolidBrush);
        gc->SetBrushColor(KRgbBlack);
        gc->Clear(Rect());
    }
}

void CGbaScreenView::AbortNow(
    RDirectScreenAccess::TTerminationReasons /*aReason*/
)
{
    iDsaActive = EFalse;
}

TInt CGbaScreenView::TickCallback(TAny* aPtr)
{
    static_cast<CGbaScreenView*>(aPtr)->OnFrameTick();
    return 1;
}

void CGbaScreenView::OnFrameTick()
{
    // Sinkronkan status tombol QWERTY/D-Pad ke register KEYINPUT (0x04000130).
    NgbaMemoryWrite16(0x04000130u, iKeyInput);

    // Eksekusi 1 frame penuh (60 FPS tanpa frameskip).
    NgbaPpuRunFrame(iCpu);

    if (iDsaActive)
        BlitFrameToScreen();
}

void CGbaScreenView::BlitFrameToScreen()
{
    const TUint16* src = NgbaPpuGetFrameBufferRgb555();

    // 1. Konversi RGB555 -> XRGB8888 sekaligus integer scaling 2x (480x320).
    iFrameBitmap->LockHeap();
    TUint32* dstBase = reinterpret_cast<TUint32*>(iFrameBitmap->DataAddress());

    for (TInt y = 0; y < KGbaScreenHeight; ++y)
    {
        const TUint16* srcRow = src + y * KGbaScreenWidth;
        TUint32* dstRow0 = dstBase + (y * 2) * KScaledWidth;
        TUint32* dstRow1 = dstRow0 + KScaledWidth;

        for (TInt x = 0; x < KGbaScreenWidth; ++x)
        {
            const TUint32 pixel = iRgb555To888Lut[srcRow[x] & 0x7FFFu];
            const TInt dx = x * 2;
            dstRow0[dx]     = pixel;
            dstRow0[dx + 1] = pixel;
            dstRow1[dx]     = pixel;
            dstRow1[dx + 1] = pixel;
        }
    }

    iFrameBitmap->UnlockHeap();

    // 2. Pusatkan 480x320 pada layar VGA 640x480 Nokia E6 (offset X=80, Y=80).
    const TRect screenRect = Rect();
    const TInt posX = (screenRect.Width() - KScaledWidth) / 2;
    const TInt posY = (screenRect.Height() - KScaledHeight) / 2;

    // 3. BitBlt langsung tanpa scaling GDI karena ukuran bitmap sudah 480x320.
    CFbsBitGc* gc = iDsa->Gc();
    gc->BitBlt(TPoint(posX, posY), iFrameBitmap);

    iDsa->ScreenDevice()->Update();
    iCoeEnv->WsSession().Flush();
}

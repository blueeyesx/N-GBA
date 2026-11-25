// gba_screen_view.h
#ifndef GBA_SCREEN_VIEW_H
#define GBA_SCREEN_VIEW_H

#include <coecntrl.h>
#include <w32std.h>
#include <fbs.h>
#include "arm7tdmi.h"

class CGbaScreenView : public CCoeControl, public MDirectScreenAccess
{
public:
    static CGbaScreenView* NewL(const TRect& aRect, TNgbaCpuState& aCpu);
    virtual ~CGbaScreenView();

    void StartEmulation();
    void StopEmulation();
    TUint16 KeyInputState() const;

private:
    CGbaScreenView(TNgbaCpuState& aCpu);
    void ConstructL(const TRect& aRect);
    void BuildColorLut();

    // CCoeControl
    void Draw(const TRect& aRect) const;
    TKeyResponse OfferKeyEventL(const TKeyEvent& aKeyEvent, TEventCode aType);
    void HandleResourceChange(TInt aType);

    // MDirectScreenAccess
    void Restart(RDirectScreenAccess::TTerminationReasons aReason);
    void AbortNow(RDirectScreenAccess::TTerminationReasons aReason);

    static TInt TickCallback(TAny* aPtr);
    void OnFrameTick();
    void BlitFrameToScreen();

private:
    TNgbaCpuState& iCpu;
    CDirectScreenAccess* iDsa;
    CFbsBitmap* iFrameBitmap;
    CPeriodic* iTimer;
    TUint32* iRgb555To888Lut;
    TUint16 iKeyInput; // Aktif rendah (0 = ditekan) sesuai register KEYINPUT GBA
    TBool iDsaActive;
};

#endif // GBA_SCREEN_VIEW_H

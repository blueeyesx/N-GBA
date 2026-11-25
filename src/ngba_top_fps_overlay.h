#ifndef NGBA_TOP_FPS_OVERLAY_H
#define NGBA_TOP_FPS_OVERLAY_H

#include <coecntrl.h>
#include <e32std.h>
#include "gba_fps_setting.h"

// Kontrol / helper untuk menampilkan FPS di pojok kiri atas layar (di luar framebuffer game).
class CNgbaTopFpsOverlay : public CCoeControl
{
public:
    static CNgbaTopFpsOverlay* NewL(CCoeControl& aParent, const TRect& aScreenRect);
    static CNgbaTopFpsOverlay* NewLC(CCoeControl& aParent, const TRect& aScreenRect);
    ~CNgbaTopFpsOverlay();

    void SetShowFps(TBool aEnabled);
    TBool ShowFps() const;
    void RefreshFps();

private:
    CNgbaTopFpsOverlay();
    void ConstructL(CCoeControl& aParent, const TRect& aScreenRect);
    void Draw(const TRect& aRect) const;
};

#endif // NGBA_TOP_FPS_OVERLAY_H

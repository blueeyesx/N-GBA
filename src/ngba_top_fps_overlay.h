#ifndef NGBA_TOP_FPS_OVERLAY_H
#define NGBA_TOP_FPS_OVERLAY_H

#include <w32std.h>
#include <fbs.h>
#include "gba_fps_setting.h"

// 1) Gambar FPS langsung ke buffer layar penuh (misal CFbsBitmap layar 640x480) di ujung kiri atas (x=6, y=6),
//    di luar area gambar game yang ada di tengah layar. Aman 100% dari konflik Window Server / DSA.
void NgbaDrawTopLeftFpsToBitmap(CFbsBitmap& aScreenBitmap);

// 2) Gambar FPS ke CGraphicsContext di ujung kiri atas layar tanpa SetClippingRect / CEikonEnv.
void NgbaDrawTopLeftFpsToGc(CGraphicsContext& aGc, const TRect& aScreenRect);

#endif // NGBA_TOP_FPS_OVERLAY_H

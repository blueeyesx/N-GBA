#ifndef NGBA_TOP_FPS_OVERLAY_H
#define NGBA_TOP_FPS_OVERLAY_H

#include <w32std.h>
#include <fbs.h>
#include "gba_fps_setting.h"

// Gambar FPS di ujung kiri atas layar penuh (di luar kotak game) ke CFbsBitmap.
void NgbaDrawTopLeftFpsToBitmap(CFbsBitmap& aScreenBitmap);

// Gambar FPS di ujung kiri atas layar penuh (di luar kotak game) ke CGraphicsContext.
void NgbaDrawTopLeftFpsToGc(CGraphicsContext& aGc, const TRect& aScreenRect);

// Helper otomatis: menggambar FPS di ujung kiri atas layar lewat CWsScreenDevice / CWindowGc aktif.
void NgbaDrawTopLeftFpsAuto();

#endif // NGBA_TOP_FPS_OVERLAY_H

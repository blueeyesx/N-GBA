#ifndef NGBA_TOP_FPS_OVERLAY_H
#define NGBA_TOP_FPS_OVERLAY_H

#include <w32std.h>
#include "gba_fps_setting.h"

// Menggambar teks FPS di pojok kiri atas pada CGraphicsContext yang sudah ada (tanpa child window/CCoeControl).
void NgbaDrawTopLeftFpsToGc(CGraphicsContext& aGc, const TRect& aScreenRect);

#endif // NGBA_TOP_FPS_OVERLAY_H

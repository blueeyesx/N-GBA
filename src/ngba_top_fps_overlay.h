#ifndef NGBA_TOP_FPS_OVERLAY_H
#define NGBA_TOP_FPS_OVERLAY_H

#include <w32std.h>
#include "gba_fps_setting.h"

// Stub aman: rendering FPS dipindahkan ke jalur bitmap PPU agar tidak bentrok dengan DSA / CWindowGc.
void NgbaDrawTopLeftFpsToGc(CGraphicsContext& aGc, const TRect& aScreenRect);

#endif // NGBA_TOP_FPS_OVERLAY_H

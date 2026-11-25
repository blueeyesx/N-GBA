#include "ngba_top_fps_overlay.h"

void NgbaDrawTopLeftFpsToGc(CGraphicsContext& /*aGc*/, const TRect& /*aScreenRect*/)
{
    // Sengaja dikosongkan agar tidak memicu redraw/clipping pada CWindowGc / DirectScreenAccess.
}

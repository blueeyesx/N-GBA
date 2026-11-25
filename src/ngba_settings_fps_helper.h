#ifndef NGBA_SETTINGS_FPS_HELPER_H
#define NGBA_SETTINGS_FPS_HELPER_H

#include <e32def.h>
#include <e32des.h>
#include "gba_fps_setting.h"

// Toggle status tampilkan FPS (ON <-> OFF) dan kembalikan nilai status terbaru.
TBool NgbaToggleShowFps();

// Isi buffer teks label menu FPS, misal: "FPS: ON" / "FPS: OFF" atau "FPS: AKTIF" / "FPS: NONAKTIF".
void NgbaGetFpsMenuLabel(TDes& aDest, TBool aIndonesian = ETrue);

#endif // NGBA_SETTINGS_FPS_HELPER_H

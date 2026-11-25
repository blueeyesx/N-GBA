#ifndef NGBA_FPS_SETTING_H
#define NGBA_FPS_SETTING_H

#include <e32def.h>
#include <e32des.h>

// Aktifkan / nonaktifkan tampilan FPS di pojok kiri atas layar (di luar layar game).
void NgbaPpuSetShowFps(TBool aEnabled);
TBool NgbaPpuGetShowFps();

// Ambil nilai FPS terkini atau teks siap tampil ("FPS: XX"; kosong jika nonaktif).
TInt NgbaPpuGetFps();
void NgbaPpuGetFpsText(TDes& aDest);

#endif // NGBA_FPS_SETTING_H

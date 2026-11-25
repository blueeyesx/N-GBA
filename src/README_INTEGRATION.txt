PANDUAN SINGKAT INTEGRASI (NOKIA E6 / SYMBIAN ANNA)
=======================================================

1. Isi paket ZIP:
   - arm7tdmi.cc, gba_memory.cc, gba_ppu.cc
   - gba_fps_setting.h
   - ngba_top_fps_overlay.h, ngba_top_fps_overlay.cc
   - ngba_settings_fps_helper.h, ngba_settings_fps_helper.cc

2. Menambahkan opsi FPS di menu PENGATURAN (di samping BAHASA & UKURAN FONT):
   #include "ngba_settings_fps_helper.h"

   // Saat membuat / me-refresh label item menu:
   TBuf<32> fpsLabel;
   NgbaGetFpsMenuLabel(fpsLabel, ETrue); // "FPS: AKTIF" / "FPS: NONAKTIF"

   // Saat item FPS dipilih/ditekan:
   NgbaToggleShowFps();

3. Menampilkan FPS di ujung kiri atas layar (di luar kotak game, tanpa blank hitam/putih):
   #include "ngba_top_fps_overlay.h"

   // Pilih salah satu sesuai jalur gambar layar utama Anda:
   // - Jika memakai CFbsBitmap layar penuh:
   //     NgbaDrawTopLeftFpsToBitmap(screenBitmap);
   // - Jika memakai CGraphicsContext / Draw():
   //     NgbaDrawTopLeftFpsToGc(gc, TRect(TPoint(0, 0), TSize(640, 480)));
   // - Jika ingin panggil langsung setelah blit layar:
   //     NgbaDrawTopLeftFpsAuto();

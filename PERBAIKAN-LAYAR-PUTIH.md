# Perbaikan N-GBA untuk Nokia E6 (Symbian Anna)

## Penyebab layar putih
1. CPSR awal 0xDF (IRQ dimatikan). Game memakai VBlankIntrWait dan menunggu
   IRQ VBlank selamanya, layar tetap forced-blank = putih. Sekarang 0x1F.
2. KEYINPUT tidak pernah diisi (terbaca 0 = semua tombol ditekan). Sekarang 0x03FF.
3. DMA tidak ada (hanya dideklarasikan). Game menyalin tile/palet lewat DMA,
   jadi VRAM kosong. DMA 0-3 (immediate/VBlank/HBlank, IRQ, repeat) kini ada.
4. Timer 0-3 tidak ada. Kini ada (prescaler, cascade, IRQ overflow).
5. RFbsSession tidak pernah disambungkan; aplikasi memakai RWsSession tanpa
   CONE, sehingga CFbsBitmap::Create bisa gagal dan fungsi gambar langsung
   return (jendela tetap putih). Kini disambungkan, dan bila gagal tampil
   pesan "BITMAP ERR n" (bukan layar kosong).

## Tambahan
- Input game: keyboard (panah, Enter/Z=A, Space/X=B, A=L, S=R, C=Start,
  V=Select) dan tombol sentuh di layar (D-pad 8 arah, A, B, A+B, L, R,
  Start, Select, MENU untuk keluar).
- Baris status di bawah layar: F:frame PC:alamat DC:DISPCNT (HALT).
  DC:0080 terus-menerus berarti game masih forced-blank.
- HALTCNT, cache ROM 64x4KB (dulu 1 blok), jalur cepat RAM, siklus per
  instruksi realistis, lewati siklus saat HALT, pacing 59.7 fps.

## Keterbatasan (jujur)
- Belum diuji di perangkat Nokia E6 atau dengan ROM asli; belum dikompilasi
  dengan SDK Symbian (tidak ada akses jaringan di sini). Inti emulasi diuji
  di PC dengan program ARM uji (tests/host_core/run.sh): reproduksi layar
  putih dengan pengaturan lama, hilang dengan perbaikan.
- Belum ada: audio, penyimpanan SRAM/Flash ke file, multi-touch.
- Instruksi yang belum didukung CPU akan menampilkan "UNSUPPORTED OP:..".
- Jika build gagal (mis. RFbsSession tidak ter-link), kirim log lengkapnya.

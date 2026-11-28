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

## Pembaruan 2 (berdasarkan log dan foto dari E6)
Golden Sun dan Yu-Gi-Oh! GX sudah tampil, game lain masih putih/berhenti.

- IRQ bersarang: flag trampoline IRQ tadinya satu boolean sehingga IRQ yang
  bersarang merusak return (PC lompat ke area stack, seperti Crash Nitro Kart
  di PC:03007DAC). Sekarang berupa penghitung kedalaman.
- Kondisi ARM 0xF (NV) tadinya "UNSUPPORTED" dan menghentikan CPU; sekarang NOP
  seperti ARM7TDMI asli.
- SWI sound driver/lainnya (0x1A-0x1E, 0x20-0x24, 0x27-0x2A) tidak lagi
  menghentikan CPU.
- HALTCNT: hanya bit7=0 (Halt) yang menghentikan CPU; Stop diabaikan.
- Penyimpanan cartridge: SRAM, Flash 64K/128K (ID, program, hapus sektor,
  bank) dan EEPROM (lewat DMA3) kini diemulasikan. Tipe dideteksi otomatis
  dari penanda library di ROM (FLASH1M_V, EEPROM_V, dst). Data disimpan ke
  file .sav di samping ROM (tiap ~5 detik jika berubah, dan saat keluar game).
  Banyak game (Pokemon, Harvest Moon, Crash, Sims) menunggu chip save saat
  boot, sehingga dulu layarnya tetap putih.
- Kecepatan: -O2 pada build, jalur cepat baca ROM 16/32-bit, frame skip
  otomatis (maks 2 frame beruntun) saat emulasi lebih lambat dari 59,7 fps.
- Status dua baris di layar: F/PC/DC dan IE/IF/IME/CP (CPSR). Jika masih putih,
  foto kedua baris itu (dan sebutkan nama game) agar penyebabnya bisa dicari.

Belum bisa dipastikan: game yang putih dengan "DC:0080 HALT" (menunggu IRQ
yang tak pernah datang). Tanpa ROM-nya saya hanya bisa menambah diagnosa.

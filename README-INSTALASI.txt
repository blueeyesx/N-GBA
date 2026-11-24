N-GBA NATIVE GUI v1 — PETUNJUK PEMASANGAN

PENTING
- Paket ini adalah GUI awal, bukan emulator GBA lengkap.
- Menu Load ROM, Resume Game, Save States, Settings, dan Game Library belum
  menjalankan fungsi emulator; tahap ini menguji GUI Window Server dan tombol.
- SIS belum diuji pada perangkat Nokia. Tunggu GitHub Actions berhasil.
- Workflow diagnostik lama ngba-symbian-platform.yml tidak diubah.

FILE YANG DITEMPATKAN DI REPOSITORI blueeyesx/N-GBA (branch main)
1. CMakeLists.txt
2. symbian.toml
3. src/main.cpp
4. .github/workflows/ngba-native-gui.yml

CARA MELALUI CHROME ANDROID
1. Buka https://github.com/blueeyesx/N-GBA
2. Pastikan branch main.
3. Unggah/ganti CMakeLists.txt di root.
4. Unggah symbian.toml di root.
5. Unggah main.cpp ke folder src/ (ganti file jika sudah ada).
6. Buat file workflow:
   .github/workflows/ngba-native-gui.yml
7. Commit perubahan.
8. Buka tab Actions.
9. Pilih "N-GBA - Native GUI v1", lalu Run workflow.
10. Tunggu kedua job selesai. Unduh artifact:
    - N-GBA-Native-GUI-S60v3-FP1-FP2-ARMv5T
    - N-GBA-Native-GUI-Symbian-Anna-ARMv6

JANGAN
- Jangan hapus workflow diagnostik lama.
- Jangan mengganti file diagnostic/**.
- Jangan memasang SIS jika build gagal atau artifact SIS tidak tersedia.

TOMBOL
- Atas/bawah: memilih menu.
- Tengah/Enter: memilih item (saat ini hanya About yang keluar).
- Backspace/Escape: keluar.

Jika build gagal, kirim log lengkap dari job yang gagal agar API/SDK dapat
disesuaikan. Jangan menganggap build berhasil hanya karena workflow terpicu.

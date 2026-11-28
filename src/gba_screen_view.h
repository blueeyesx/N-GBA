
#ifndef GBA_SCREEN_VIEW_H
#define GBA_SCREEN_VIEW_H

#include <e32std.h>
#include <w32std.h>
#include <fbs.h>

// Bit tombol GBA (sama dengan NgbaMemorySetKeys).
const TUint16 KNgbaKeyA      = 0x0001;
const TUint16 KNgbaKeyB      = 0x0002;
const TUint16 KNgbaKeySelect = 0x0004;
const TUint16 KNgbaKeyStart  = 0x0008;
const TUint16 KNgbaKeyRight  = 0x0010;
const TUint16 KNgbaKeyLeft   = 0x0020;
const TUint16 KNgbaKeyUp     = 0x0040;
const TUint16 KNgbaKeyDown   = 0x0080;
const TUint16 KNgbaKeyR      = 0x0100;
const TUint16 KNgbaKeyL      = 0x0200;

// Bit khusus (bukan tombol GBA): tombol layar "MENU" untuk keluar game.
const TUint16 KNgbaKeyMenu   = 0x8000;

// Menyambung ke Font & Bitmap Server lalu membuat bitmap 240x160.
// Mengembalikan kode error (KErrNone jika berhasil).
TInt NgbaScreenViewInitialize();

// Menghapus bitmap dan memutus sesi Font & Bitmap Server.
void NgbaScreenViewShutdown();

// Kode error terakhir dari inisialisasi (untuk ditampilkan di layar).
TInt NgbaScreenViewError();

// Menggambar framebuffer GBA + tombol sentuh ke Window Server.
// aPressedMask = tombol yang sedang ditekan (untuk sorotan).
void NgbaScreenViewDraw(
    CWindowGc& aGc,
    const TRect& aTargetRect,
    CFont* aFont,
    TUint16 aPressedMask
);

// Area gambar game di dalam aSize (untuk invalidasi / diagnosa).
TRect NgbaScreenViewGameRect(const TSize& aSize);

// Area di bawah layar game yang dipakai untuk status teks.
TRect NgbaScreenViewControlsRect(const TSize& aSize);

// Mengembalikan mask tombol di titik sentuh (0 jika tidak ada).
TUint16 NgbaScreenViewHitTest(
    const TPoint& aPoint,
    const TSize& aSize
);

// Teks status satu baris yang ditampilkan di area kontrol.
void NgbaScreenViewSetStatus(const TDesC& aText);

// Teks status baris kedua (IE/IF/IME/CPSR).
void NgbaScreenViewSetStatus2(const TDesC& aText);

#endif

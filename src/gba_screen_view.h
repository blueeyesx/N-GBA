
#ifndef GBA_SCREEN_VIEW_H
#define GBA_SCREEN_VIEW_H

#include <e32std.h>
#include <w32std.h>
#include <fbs.h>

// Membuat bitmap untuk menampilkan framebuffer GBA.
TInt NgbaScreenViewInitialize();

// Menghapus bitmap dan membebaskan memori.
void NgbaScreenViewShutdown();

// Menggambar framebuffer GBA ke Window Server.
void NgbaScreenViewDraw(
    CWindowGc& aGc,
    const TRect& aTargetRect
);

#endif

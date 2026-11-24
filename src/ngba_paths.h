
#ifndef NGBA_PATHS_H
#define NGBA_PATHS_H

#include <e32std.h>

// Inisialisasi direktori utama.
// Prioritas E:\GBA\, kemudian C:\GBA\.
TInt NgbaPathsInit();

// Mengembalikan drive yang sedang digunakan.
TChar NgbaPathsDrive();

// Direktori utama.
void NgbaGetRootPath(TDes& aPath);

// Direktori ROM.
void NgbaGetRomPath(TDes& aPath);

// Direktori Save.
void NgbaGetSavePath(TDes& aPath);

// Direktori Save State.
void NgbaGetSaveStatePath(TDes& aPath);

// Direktori Config.
void NgbaGetConfigPath(TDes& aPath);

// Direktori Log.
void NgbaGetLogPath(TDes& aPath);

// Direktori Settings.
void NgbaGetSettingsPath(TDes& aPath);

// Direktori Covers.
void NgbaGetCoversPath(TDes& aPath);

// Direktori Wallpapers.
void NgbaGetWallpapersPath(TDes& aPath);

// Direktori Cheats.
void NgbaGetCheatsPath(TDes& aPath);

// Status inisialisasi.
TBool NgbaPathsReady();

#endif

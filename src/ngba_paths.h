
#ifndef NGBA_PATHS_H
#define NGBA_PATHS_H

#include <e32std.h>

// Inisialisasi direktori N-GBA.
// Prioritas E:\GBA\, fallback C:\GBA\.
TInt NgbaPathsInit();

// Status inisialisasi.
TBool NgbaPathsReady();

// Drive aktif: E atau C.
TChar NgbaPathsDrive();

// Direktori utama.
void NgbaGetRootPath(TDes& aPath);

// Direktori BIOS.
void NgbaGetBiosPath(TDes& aPath);

// Direktori konfigurasi.
void NgbaGetConfigPath(TDes& aPath);

// Direktori save.
void NgbaGetSavePath(TDes& aPath);

// Direktori save state.
void NgbaGetSaveStatePath(TDes& aPath);

// Direktori log.
void NgbaGetLogPath(TDes& aPath);

// Direktori bahasa.
void NgbaGetLanguagePath(TDes& aPath);

// Direktori font.
void NgbaGetFontPath(TDes& aPath);

// Direktori cover.
void NgbaGetCoversPath(TDes& aPath);

// Direktori wallpaper.
void NgbaGetWallpapersPath(TDes& aPath);

// Direktori cheat.
void NgbaGetCheatsPath(TDes& aPath);

// Direktori data tambahan.
void NgbaGetDataPath(TDes& aPath);

// Lokasi file bahasa.
void NgbaGetLanguageFilePath(TDes& aPath);

// Lokasi file font.
void NgbaGetFontFilePath(TDes& aPath);

// Lokasi file log.
void NgbaGetLogFilePath(TDes& aPath);

// Lokasi BIOS default.
void NgbaGetBiosFilePath(TDes& aPath);

#endif


#ifndef GBA_PPU_H
#define GBA_PPU_H

#include <e32std.h>
#include "arm7tdmi.h"

const TInt KGbaScreenWidth  = 240;
const TInt KGbaScreenHeight = 160;
const TInt KGbaScreenPixels = KGbaScreenWidth * KGbaScreenHeight;

typedef void (*TNgbaPpuIrqCallback)(TUint16 aIrqFlag);
typedef void (*TNgbaPpuEventCallback)();

void NgbaPpuReset();

void NgbaPpuSetIrqCallback(TNgbaPpuIrqCallback aCallback);
void NgbaPpuSetVBlankCallback(TNgbaPpuEventCallback aCallback);
void NgbaPpuSetHBlankCallback(TNgbaPpuEventCallback aCallback);

TUint8  NgbaPpuReadIo8(TUint32 aAddress);
TUint16 NgbaPpuReadIo16(TUint32 aAddress);
TUint32 NgbaPpuReadIo32(TUint32 aAddress);

void NgbaPpuWriteIo8(TUint32 aAddress, TUint8 aValue);
void NgbaPpuWriteIo16(TUint32 aAddress, TUint16 aValue);
void NgbaPpuWriteIo32(TUint32 aAddress, TUint32 aValue);

// Menjalankan satu frame GBA.
// Mengembalikan status CPU jika ditemukan instruksi yang bermasalah.
TInt NgbaPpuRunFrame(TNgbaCpuState& aCpu);

void NgbaPpuRenderLine(TInt aLine);

const TUint16* NgbaPpuGetFrameBufferRgb555();

// Mematikan render scanline (frame skip) tanpa menghentikan emulasi.
void NgbaPpuSetRenderEnabled(TBool aEnabled);

// Nilai register DISPCNT saat ini (untuk diagnosa di layar).
TUint16 NgbaPpuGetDispCnt();

void NgbaPpuConvertToRgb565(TUint16* aDestRgb565);
void NgbaPpuConvertToXrgb8888(TUint32* aDestXrgb8888);

#endif

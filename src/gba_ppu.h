#ifndef GBA_PPU_H
#define GBA_PPU_H

#include <e32std.h>
#include "arm7tdmi.h"

const TInt KGbaScreenWidth  = 240;
const TInt KGbaScreenHeight = 160;
const TInt KGbaScreenPixels = KGbaScreenWidth * KGbaScreenHeight;

// Callback opsional untuk menghubungkan PPU ke IRQ controller dan DMA tanpa
// ketergantungan header melingkar.
typedef void (*TNgbaPpuIrqCallback)(TUint16 aIrqFlag);
typedef void (*TNgbaPpuEventCallback)();

void NgbaPpuReset();

void NgbaPpuSetIrqCallback(TNgbaPpuIrqCallback aCallback);
void NgbaPpuSetVBlankCallback(TNgbaPpuEventCallback aCallback);
void NgbaPpuSetHBlankCallback(TNgbaPpuEventCallback aCallback);

// Akses register I/O PPU (0x04000000 - 0x04000056).
TUint8  NgbaPpuReadIo8(TUint32 aAddress);
TUint16 NgbaPpuReadIo16(TUint32 aAddress);
TUint32 NgbaPpuReadIo32(TUint32 aAddress);

void NgbaPpuWriteIo8(TUint32 aAddress, TUint8 aValue);
void NgbaPpuWriteIo16(TUint32 aAddress, TUint16 aValue);
void NgbaPpuWriteIo32(TUint32 aAddress, TUint32 aValue);

// Jalankan 1 frame penuh GBA (228 scanline x 1232 siklus = 280.896 siklus).
void NgbaPpuRunFrame(TNgbaCpuState& aCpu);

// Render 1 scanline secara manual (jika timing frame dikelola di luar PPU).
void NgbaPpuRenderLine(TInt aLine);

// Akses framebuffer internal RGB555 (240x160).
const TUint16* NgbaPpuGetFrameBufferRgb555();

// Konversi framebuffer ke format CFbsBitmap Symbian Anna (Nokia E6):
// - EColor64K  -> 16-bit RGB565
// - EColor16MU -> 32-bit XRGB8888 (mode layar default Symbian^3 / Anna)
void NgbaPpuConvertToRgb565(TUint16* aDestRgb565);
void NgbaPpuConvertToXrgb8888(TUint32* aDestXrgb8888);

#endif // GBA_PPU_H

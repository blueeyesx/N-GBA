#include <stdio.h>
#include <stdlib.h>
#include "e32std.h"
#include "arm7tdmi.h"
#include "gba_memory.h"
#include "gba_ppu.h"
static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){printf("  FAIL: %s\n",msg);fails++;} else printf("  ok:   %s\n",msg);}while(0)
static TUint8 rom[0x1000];
static TUint16 px(int x,int y){ return NgbaPpuGetFrameBufferRgb555()[y*240+x]; }
int runBoot(TUint32 cpsrOverride, int frames, TUint16& pxOut, TNgbaCpuState& cpu){
    FILE* f=fopen("boot.rom","rb"); fread(rom,1,sizeof rom,f); fclose(f);
    NgbaMemoryInit(rom,sizeof rom);
    NgbaCpuReset(cpu); NgbaPpuReset();
    if(cpsrOverride) cpu.cpsr=cpsrOverride;
    int r=0;
    for(int i=0;i<frames;i++){ r=NgbaPpuRunFrame(cpu); if(r!=ENgbaCpuOk){printf("  cpu stop %d at pc=%08x op=%08x\n",r,cpu.lastAddress,cpu.lastOpcode);break;} }
    pxOut=px(10,10);
    return r;
}
int main(){
    printf("[1] Boot + VBlankIntrWait, CPSR lama (0xDF = IRQ dimatikan)\n");
    { TNgbaCpuState cpu; TUint16 p; runBoot(0xDF,10,p,cpu);
      printf("  pixel=%04x halted=%d pc=%08x\n",p,cpu.halted,cpu.r[15]);
      CHECK(p==0x7FFF,"perilaku lama: layar tetap PUTIH (forced blank), reproduksi bug"); }
    printf("[2] Boot + VBlankIntrWait, CPSR baru (0x1F)\n");
    { TNgbaCpuState cpu; TUint16 p; runBoot(0,10,p,cpu);
      printf("  pixel=%04x halted=%d pc=%08x cpsr=%08x\n",p,cpu.halted,cpu.r[15],cpu.cpsr);
      CHECK(p==0x001F,"layar terbuka & backdrop merah tampil"); }
    printf("[3] KEYINPUT\n");
    { NgbaMemoryInit(rom,sizeof rom);
      CHECK(NgbaMemoryRead16(0x04000130)==0x03FF,"default: tidak ada tombol ditekan (0x03FF)");
      NgbaMemorySetKeys(0x0001|0x0008);
      CHECK(NgbaMemoryRead16(0x04000130)==(0x03FF&~0x0009),"A+Start tertekan");
      NgbaMemoryWrite16(0x04000130,0); CHECK(NgbaMemoryRead16(0x04000130)==(0x03FF&~0x0009),"KEYINPUT read-only"); }
    printf("[4] DMA3 immediate EWRAM->VRAM (32-bit)\n");
    { NgbaMemoryInit(rom,sizeof rom);
      for(int i=0;i<64;i++) NgbaMemoryWrite32(0x02000000+i*4,0x11110000u+i);
      NgbaMemoryWrite32(0x040000D4,0x02000000); NgbaMemoryWrite32(0x040000D8,0x06000000);
      NgbaMemoryWrite32(0x040000DC,64|(0x8400u<<16));
      CHECK(NgbaMemoryRead32(0x06000000)==0x11110000u && NgbaMemoryRead32(0x060000FC)==0x1111003Fu,"64 word tersalin ke VRAM");
      CHECK((NgbaMemoryRead16(0x040000DE)&0x8000)==0,"bit enable DMA dibersihkan setelah selesai"); }
    printf("[5] DMA timing VBlank (repeat) + IRQ\n");
    { NgbaMemoryInit(rom,sizeof rom);
      NgbaMemoryWrite16(0x02000000,0xABCD);
      NgbaMemoryWrite32(0x040000B0,0x02000000); NgbaMemoryWrite32(0x040000B4,0x05000000);
      NgbaMemoryWrite32(0x040000B8,1|((0x8000u|(1<<12)|(1<<9)|(1<<14))<<16));
      CHECK(NgbaMemoryRead16(0x05000000)==0,"belum jalan sebelum VBlank");
      NgbaMemoryRunDma(1);
      CHECK(NgbaMemoryRead16(0x05000000)==0xABCD,"jalan saat VBlank");
      CHECK((NgbaMemoryRead16(0x04000202)&0x0100)!=0,"IRQ DMA0 diangkat");
      CHECK((NgbaMemoryRead16(0x040000BA)&0x8000)!=0,"repeat: tetap aktif"); }
    printf("[6] Timer overflow IRQ\n");
    { NgbaMemoryInit(rom,sizeof rom);
      NgbaMemoryWrite32(0x04000100,0xFFF0u|((0x80u|0x40u)<<16));
      NgbaMemoryTickTimers(8); CHECK((NgbaMemoryRead16(0x04000202)&8)==0,"belum overflow");
      NgbaMemoryTickTimers(16); CHECK((NgbaMemoryRead16(0x04000202)&8)!=0,"overflow -> IF timer0");
      CHECK(NgbaMemoryRead16(0x04000100)>=0xFFF0,"counter reload"); }
    printf("[7] Mode 0 tile BG dari DMA (palet + tile + map)\n");
    { NgbaMemoryInit(rom,sizeof rom); NgbaPpuReset();
      NgbaMemoryWrite16(0x05000002,0x03E0); // idx1 hijau
      for(int i=0;i<8;i++) NgbaMemoryWrite32(0x06000020+i*4,0x11111111u); // tile1 = semua idx1 (4bpp)
      NgbaMemoryWrite16(0x06004000+2*0,1); // map (0,0)=tile1, screenbase 8 -> 0x4000
      NgbaMemoryWrite16(0x04000008,(8<<8));   // BG0CNT charbase0 screenbase8
      NgbaMemoryWrite16(0x04000000,0x0100);   // mode0 BG0
      NgbaPpuRenderLine(0);
      CHECK(px(0,0)==0x03E0,"tile BG tergambar (hijau)");
      CHECK(px(16,0)==0x0000,"area tanpa tile = backdrop"); }
    printf("\n%s (%d gagal)\n",fails?"ADA YANG GAGAL":"SEMUA LULUS",fails);
    return fails;
}

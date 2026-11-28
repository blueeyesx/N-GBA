def mov(rd,imm,rot=0): return 0xE3A00000|rd<<12|rot<<8|imm
def add(rd,rn,imm,rot=0): return 0xE2800000|rn<<16|rd<<12|rot<<8|imm
def orr(rd,rn,imm,rot=0): return 0xE3800000|rn<<16|rd<<12|rot<<8|imm
def strh(rd,rn,off): return 0xE1C000B0|rn<<16|rd<<12|((off>>4)<<8)|(off&0xF)
def str_(rd,rn,off): return 0xE5800000|rn<<16|rd<<12|off
def ldr(rd,rn,off): return 0xE5900000|rn<<16|rd<<12|off
def swi(n): return 0xEF000000|n<<16
def b(cur,target): return 0xEA000000|(((target-(cur+8))>>2)&0xFFFFFF)
BXLR=0xE12FFF1E
words={}
def place(addr,lst):
    for i,w in enumerate(lst): words[addr+4*i]=w
# main @0
pc=0
main=[
 mov(0,4,4),            # r0=0x04000000
 mov(1,0x80),           # r1=0x80
 strh(1,0,0),           # DISPCNT=forced blank
 mov(2,3,4),            # r2=0x03000000
 add(2,2,0x7F,0xC),     # r2=0x03007F00
 mov(3,8,4),            # r3=0x08000000
 add(3,3,1,0xC),        # r3=0x08000100 (handler)
 str_(3,2,0xFC),        # [0x03007FFC]=handler
 add(4,0,2,0xC),        # r4=0x04000200
 mov(1,1),              # r1=1
 strh(1,4,0),           # IE=1 (VBlank)
 strh(1,4,8),           # IME=1
 mov(5,8),              # r5=8
 strh(5,0,4),           # DISPSTAT: VBlank IRQ enable
 swi(5),                # VBlankIntrWait
 mov(6,5,4),            # r6=0x05000000
 mov(7,0x1F),           # red
 strh(7,6,0),           # palette[0]=red
 mov(1,0),
 strh(1,0,0),           # DISPCNT=0 -> layar aktif
]
loop_addr=4*len(main)
main+= [swi(5), b(loop_addr+4,loop_addr)]
place(0,main)
handler=[
 mov(1,1),
 add(2,0,2,0xC),        # r2=0x04000200
 strh(1,2,2),           # IF ack
 mov(3,3,4),
 add(3,3,0x7F,0xC),     # r3=0x03007F00
 ldr(4,3,0xF8),
 orr(4,4,1),
 str_(4,3,0xF8),
 BXLR,
]
place(0x100,handler)
size=0x200
import struct
rom=bytearray(0x1000)
for a,w in words.items(): rom[a:a+4]=struct.pack('<I',w)
open('boot.rom','wb').write(rom)
print(len(words),'words')

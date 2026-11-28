#!/bin/sh
# Tes inti emulasi di PC (bukan di Symbian). Jalankan dari folder ini.
set -e
S=../../src
python3 asm.py
for f in gba_memory arm7tdmi gba_ppu; do
  g++ -std=c++11 -fpermissive -Wno-narrowing -Ishim -I$S -c $S/$f.cc -o $f.o
done
g++ -std=c++11 -fpermissive -Wno-narrowing -Ishim -I$S test.cpp gba_memory.o arm7tdmi.o gba_ppu.o -o t
./t

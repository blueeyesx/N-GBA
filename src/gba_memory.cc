
#include "gba_memory.h"
#include "gba_ppu.h"

#include <e32std.h>
#include <f32file.h>

namespace
{
    const TInt KEwramSize = 0x40000;
    const TInt KIwramSize = 0x8000;
    const TInt KIoSize = 0x400;
    const TInt KPaletteSize = 0x400;
    const TInt KVramSize = 0x18000;
    const TInt KOamSize = 0x400;
    const TInt KSramSize = 0x10000;

    // Cache ROM: direct-mapped, 64 slot x 4 KB = 256 KB.
    // (Sebelumnya hanya 1 blok 4 KB, sehingga fetch kode dan baca data
    //  ROM bergantian memicu pembacaan file di setiap akses.)
    const TInt KRomCacheSize = 4096;
    const TInt KRomCacheSlots = 64;

    const TUint32 KEwramBase = 0x02000000u;
    const TUint32 KIwramBase = 0x03000000u;
    const TUint32 KIoBase = 0x04000000u;
    const TUint32 KPaletteBase = 0x05000000u;
    const TUint32 KVramBase = 0x06000000u;
    const TUint32 KOamBase = 0x07000000u;
    const TUint32 KRomBase = 0x08000000u;
    const TUint32 KSramBase = 0x0E000000u;

    const TUint32 KPpuIoStart = 0x04000000u;
    const TUint32 KPpuIoEnd = 0x04000057u;

    TUint8 gEwram[KEwramSize];
    TUint8 gIwram[KIwramSize];
    TUint8 gIo[KIoSize];
    TUint8 gPalette[KPaletteSize];
    TUint8 gVram[KVramSize];
    TUint8 gOam[KOamSize];
    // ------------------------------------------------------------
    // PENYIMPANAN CARTRIDGE (SRAM / Flash 64K / Flash 128K / EEPROM)
    // ------------------------------------------------------------
    const TInt KSaveBytes = 0x20000;

    enum
    {
        ESaveSram = 0,
        ESaveFlash64,
        ESaveFlash128,
        ESaveEeprom
    };

    TUint8 gSave[KSaveBytes];
    TInt gSaveType = ESaveSram;
    TBool gSaveDirty = EFalse;
    TBool gSavePathValid = EFalse;
    TFileName gSavePath;

    // Mesin status Flash.
    TInt gFlashState = 0;
    TBool gFlashIdMode = EFalse;
    TBool gFlashEraseArmed = EFalse;
    TBool gFlashProgramNext = EFalse;
    TBool gFlashBankNext = EFalse;
    TInt gFlashBank = 0;

    // EEPROM (diakses lewat DMA3 ke 0x0D000000).
    TInt gEepromAddrBits = 14;
    TUint32 gEepromReadAddr = 0;

    const TUint8* gRom = NULL;

    RFs gRomFs;
    RFile gRomFile;
    TBool gRomFileOpen = EFalse;

    TUint8 gRomCache[KRomCacheSlots][KRomCacheSize];
    TInt gRomCacheTag[KRomCacheSlots];   // nomor blok, -1 = kosong
    TInt gRomCacheLen[KRomCacheSlots];

    void FlushRomCache()
    {
        for (TInt i = 0; i < KRomCacheSlots; ++i)
        {
            gRomCacheTag[i] = -1;
            gRomCacheLen[i] = 0;
        }
    }

    // --- Keypad, DMA, Timer, Halt ---
    TBool gHaltRequest = EFalse;

    TBool gDmaEnabled[4];
    TUint32 gDmaSrc[4];
    TUint32 gDmaDst[4];
    TUint32 gDmaCount[4];

    TBool gTimerEnabled[4];
    TUint32 gTimerCounter[4];
    TUint32 gTimerFrac[4];
    const TInt KTimerDiv[4] = { 1, 64, 256, 1024 };

    TInt gRomSize = 0;
    TBool gReady = EFalse;

    TBool IsPpuIoAddress(TUint32 aAddress)
    {
        return (
            aAddress >= KPpuIoStart &&
            aAddress <= KPpuIoEnd
        );
    }

    void CloseRomFile()
    {
        if (gRomFileOpen)
        {
            gRomFile.Close();
            gRomFileOpen = EFalse;
        }

        gRomFs.Close();
    }

    void ClearMemory()
    {
        Mem::FillZ(gEwram, sizeof(gEwram));
        Mem::FillZ(gIwram, sizeof(gIwram));
        Mem::FillZ(gIo, sizeof(gIo));
        Mem::FillZ(gPalette, sizeof(gPalette));
        Mem::FillZ(gVram, sizeof(gVram));
        Mem::FillZ(gOam, sizeof(gOam));
        // Flash dan EEPROM kosong bernilai 0xFF.
        memset(gSave, 0xFF, sizeof(gSave));
        gSaveType = ESaveSram;
        gSaveDirty = EFalse;
        gSavePathValid = EFalse;
        gFlashState = 0;
        gFlashIdMode = EFalse;
        gFlashEraseArmed = EFalse;
        gFlashProgramNext = EFalse;
        gFlashBankNext = EFalse;
        gFlashBank = 0;
        gEepromAddrBits = 14;
        gEepromReadAddr = 0;

        // KEYINPUT: aktif-rendah, 0x03FF = tidak ada tombol ditekan.
        gIo[0x130] = 0xFF;
        gIo[0x131] = 0x03;

        gHaltRequest = EFalse;

        for (TInt i = 0; i < 4; ++i)
        {
            gDmaEnabled[i] = EFalse;
            gDmaSrc[i] = 0;
            gDmaDst[i] = 0;
            gDmaCount[i] = 0;
            gTimerEnabled[i] = EFalse;
            gTimerCounter[i] = 0;
            gTimerFrac[i] = 0;
        }
    }

    TUint8 ReadArray(
        const TUint8* aData,
        TInt aSize,
        TUint32 aOffset
    )
    {
        if (aData == NULL || aSize <= 0)
            return 0;

        return aData[
            aOffset % static_cast<TUint32>(aSize)
        ];
    }

    void WriteArray(
        TUint8* aData,
        TInt aSize,
        TUint32 aOffset,
        TUint8 aValue
    )
    {
        if (aData == NULL || aSize <= 0)
            return;

        aData[
            aOffset % static_cast<TUint32>(aSize)
        ] = aValue;
    }

    TUint32 MapVramOffset(TUint32 aOffset)
    {
        aOffset &= 0x1FFFFu;

        if (aOffset >= 0x18000u)
            aOffset -= 0x8000u;

        return aOffset;
    }

    TUint8 ReadRom(TUint32 aAddress)
    {
        if (gRomSize <= 0)
            return 0;

        const TUint32 offset =
            aAddress - KRomBase;

        const TInt romOffset = static_cast<TInt>(
            offset % static_cast<TUint32>(gRomSize)
        );

        if (gRom != NULL)
            return gRom[romOffset];

        if (!gRomFileOpen)
            return 0;

        const TInt block = romOffset / KRomCacheSize;
        const TInt slot = block % KRomCacheSlots;
        const TInt inBlock = romOffset - block * KRomCacheSize;

        if (gRomCacheTag[slot] != block)
        {
            const TInt blockStart = block * KRomCacheSize;

            TInt bytesToRead = gRomSize - blockStart;

            if (bytesToRead > KRomCacheSize)
                bytesToRead = KRomCacheSize;

            if (bytesToRead <= 0)
                return 0;

            TPtr8 descriptor(
                gRomCache[slot],
                0,
                KRomCacheSize
            );

            const TInt error = gRomFile.Read(
                blockStart,
                descriptor,
                bytesToRead
            );

            if (error != KErrNone)
            {
                gRomCacheTag[slot] = -1;
                gRomCacheLen[slot] = 0;
                return 0;
            }

            gRomCacheTag[slot] = block;
            gRomCacheLen[slot] = descriptor.Length();
        }

        if (inBlock >= gRomCacheLen[slot])
            return 0;

        return gRomCache[slot][inBlock];
    }

    // -----------------------------------------------------
    // FLASH / SRAM / EEPROM
    // -----------------------------------------------------

    TInt SaveFileSize()
    {
        switch (gSaveType)
        {
        case ESaveFlash64:  return 0x10000;
        case ESaveFlash128: return 0x20000;
        case ESaveEeprom:   return (gEepromAddrBits == 6) ? 0x200 : 0x2000;
        default:            return 0x8000;
        }
    }

    TUint8 ReadSave(TUint32 aOffset)
    {
        if (gSaveType == ESaveFlash64 || gSaveType == ESaveFlash128)
        {
            const TUint32 a = aOffset & 0xFFFFu;

            if (gFlashIdMode)
            {
                if (a == 0)
                    return 0xC2;    // Macronix

                if (a == 1)
                    return (gSaveType == ESaveFlash128) ? 0x09 : 0x1C;
            }

            return gSave[(gFlashBank * 0x10000 + a) & (KSaveBytes - 1)];
        }

        return gSave[aOffset & 0x7FFFu];
    }

    void FlashWrite(TUint32 aAddr, TUint8 aValue)
    {
        const TInt chipSize =
            (gSaveType == ESaveFlash128) ? 0x20000 : 0x10000;

        if (gFlashProgramNext)
        {
            gSave[(gFlashBank * 0x10000 + aAddr) & (KSaveBytes - 1)] &=
                aValue;
            gFlashProgramNext = EFalse;
            gSaveDirty = ETrue;
            return;
        }

        if (gFlashBankNext && aAddr == 0)
        {
            gFlashBank = aValue & 1;
            gFlashBankNext = EFalse;
            return;
        }

        if (gFlashState == 0)
        {
            if (aAddr == 0x5555u && aValue == 0xAA)
                gFlashState = 1;
            else if (aValue == 0xF0)
                gFlashIdMode = EFalse;

            return;
        }

        if (gFlashState == 1)
        {
            gFlashState = (aAddr == 0x2AAAu && aValue == 0x55) ? 2 : 0;
            return;
        }

        // gFlashState == 2: byte perintah.
        gFlashState = 0;

        if (gFlashEraseArmed)
        {
            gFlashEraseArmed = EFalse;

            if (aAddr == 0x5555u && aValue == 0x10)
            {
                memset(gSave, 0xFF, chipSize);
                gSaveDirty = ETrue;
            }
            else if (aValue == 0x30)
            {
                const TInt base =
                    (gFlashBank * 0x10000 + (aAddr & 0xF000u)) &
                    (KSaveBytes - 1);
                memset(gSave + base, 0xFF, 0x1000);
                gSaveDirty = ETrue;
            }

            return;
        }

        if (aAddr != 0x5555u)
            return;

        switch (aValue)
        {
        case 0x90: gFlashIdMode = ETrue; break;
        case 0xF0: gFlashIdMode = EFalse; break;
        case 0x80: gFlashEraseArmed = ETrue; break;
        case 0xA0: gFlashProgramNext = ETrue; break;
        case 0xB0:
            if (gSaveType == ESaveFlash128)
                gFlashBankNext = ETrue;
            break;
        default: break;
        }
    }

    void WriteSave(TUint32 aOffset, TUint8 aValue)
    {
        if (gSaveType == ESaveFlash64 || gSaveType == ESaveFlash128)
        {
            FlashWrite(aOffset & 0xFFFFu, aValue);
            return;
        }

        gSave[aOffset & 0x7FFFu] = aValue;
        gSaveDirty = ETrue;
    }

    TBool EepromActive()
    {
        return gSaveType == ESaveEeprom &&
               gRomSize <= 0x01000000;
    }

    void EepromProcess(const TUint8* aBits, TUint32 aCount)
    {
        TInt addrBits = 0;

        if (aCount == 9 || aCount == 73)
            addrBits = 6;
        else if (aCount == 17 || aCount == 81)
            addrBits = 14;
        else
            return;

        gEepromAddrBits = addrBits;

        const TUint32 cmd = (static_cast<TUint32>(aBits[0]) << 1) | aBits[1];

        TUint32 addr = 0;

        for (TInt i = 0; i < addrBits; ++i)
            addr = (addr << 1) | aBits[2 + i];

        if (cmd == 3)
        {
            gEepromReadAddr = addr;
        }
        else if (cmd == 2 && (aCount == 73 || aCount == 81))
        {
            for (TInt j = 0; j < 8; ++j)
            {
                TUint32 v = 0;

                for (TInt k = 0; k < 8; ++k)
                    v = (v << 1) | aBits[2 + addrBits + j * 8 + k];

                gSave[(addr * 8 + j) & 0x1FFFu] = static_cast<TUint8>(v);
            }

            gSaveDirty = ETrue;
        }
    }

    // Menangani DMA3 yang menuju/berasal dari area EEPROM. Mengembalikan
    // ETrue jika DMA sudah ditangani seluruhnya.
    TBool EepromDma(TUint32& aSrc, TUint32& aDst, TUint32 aCount)
    {
        if (!EepromActive())
            return EFalse;

        if ((aDst >> 24) == 0x0Du)
        {
            TUint8 bits[96];
            const TUint32 n = (aCount < 96u) ? aCount : 96u;

            for (TUint32 i = 0; i < n; ++i)
                bits[i] = static_cast<TUint8>(
                    NgbaMemoryRead16(aSrc + i * 2u) & 1u
                );

            aSrc += aCount * 2u;
            EepromProcess(bits, n);
            return ETrue;
        }

        if ((aSrc >> 24) == 0x0Du)
        {
            for (TUint32 i = 0; i < aCount; ++i)
            {
                TUint16 bit = 0;

                if (i >= 4u && (i - 4u) < 64u)
                {
                    const TUint32 b = i - 4u;
                    const TUint8 byte = gSave[
                        (gEepromReadAddr * 8u + (b >> 3)) & 0x1FFFu
                    ];
                    bit = static_cast<TUint16>((byte >> (7u - (b & 7u))) & 1u);
                }

                NgbaMemoryWrite16(aDst + i * 2u, bit);
            }

            aDst += aCount * 2u;
            return ETrue;
        }

        return EFalse;
    }

    // Mencari penanda library save pada ROM (EEPROM_V, FLASH1M_V, ...).
    TUint8 gScanBuffer[0x10000 + 16];

    void DetectSaveType()
    {
        gSaveType = ESaveSram;

        TInt carry = 0;
        TInt pos = 0;
        TBool sawSram = EFalse;

        while (pos < gRomSize)
        {
            TInt want = gRomSize - pos;

            if (want > 0x10000)
                want = 0x10000;

            TPtr8 des(gScanBuffer + carry, 0, 0x10000);

            if (gRomFile.Read(pos, des, want) != KErrNone ||
                des.Length() <= 0)
                break;

            const TInt n = carry + des.Length();

            for (TInt i = 0; i < n; ++i)
            {
                const TUint8 c = gScanBuffer[i];
                const TUint8* t = gScanBuffer + i;
                const TInt left = n - i;

                if (c == 'F' && left >= 9 &&
                    memcmp(t, "FLASH1M_V", 9) == 0)
                {
                    gSaveType = ESaveFlash128;
                    return;
                }

                if (c == 'F' && left >= 10 &&
                    memcmp(t, "FLASH512_V", 10) == 0)
                {
                    gSaveType = ESaveFlash64;
                    return;
                }

                if (c == 'F' && left >= 7 &&
                    memcmp(t, "FLASH_V", 7) == 0)
                {
                    gSaveType = ESaveFlash64;
                    return;
                }

                if (c == 'E' && left >= 8 &&
                    memcmp(t, "EEPROM_V", 8) == 0)
                {
                    gSaveType = ESaveEeprom;
                    return;
                }

                if (c == 'S' && left >= 6 &&
                    memcmp(t, "SRAM_V", 6) == 0)
                    sawSram = ETrue;
            }

            // Sisakan 15 byte terakhir agar penanda di batas blok terbaca.
            carry = (n >= 15) ? 15 : n;
            memmove(gScanBuffer, gScanBuffer + n - carry, carry);
            pos += des.Length();
        }

        (void)sawSram;
    }

    void BuildSavePath(const TDesC& aRomPath)
    {
        gSavePathValid = EFalse;

        if (aRomPath.Length() + 4 > gSavePath.MaxLength())
            return;

        gSavePath.Copy(aRomPath);

        const TInt dot = gSavePath.LocateReverse('.');

        if (dot >= 0)
            gSavePath.SetLength(dot);

        if (gSavePath.Length() + 4 > gSavePath.MaxLength())
            return;

        gSavePath.Append(_L(".sav"));
        gSavePathValid = ETrue;
    }

    void LoadSaveFile()
    {
        if (!gSavePathValid)
            return;

        RFile file;

        if (file.Open(gRomFs, gSavePath, EFileRead | EFileShareReadersOnly)
            != KErrNone)
            return;

        TPtr8 des(gSave, 0, KSaveBytes);
        file.Read(0, des, SaveFileSize());
        file.Close();
    }

    void FlushSaveFile()
    {
        if (!gSavePathValid || !gSaveDirty || !gRomFileOpen)
            return;

        RFile file;

        if (file.Replace(gRomFs, gSavePath, EFileWrite) != KErrNone)
            return;

        TPtrC8 data(gSave, SaveFileSize());

        if (file.Write(data) == KErrNone)
            gSaveDirty = EFalse;

        file.Close();
    }

    // -----------------------------------------------------
    // DMA
    // -----------------------------------------------------

    inline TUint16 IoU16(TInt aOffset)
    {
        return static_cast<TUint16>(
            gIo[aOffset] | (static_cast<TUint16>(gIo[aOffset + 1]) << 8)
        );
    }

    inline TUint32 IoU32(TInt aOffset)
    {
        return static_cast<TUint32>(gIo[aOffset]) |
               (static_cast<TUint32>(gIo[aOffset + 1]) << 8) |
               (static_cast<TUint32>(gIo[aOffset + 2]) << 16) |
               (static_cast<TUint32>(gIo[aOffset + 3]) << 24);
    }

    TUint32 DmaCountFromRegister(TInt aChannel)
    {
        const TInt base = 0xB0 + aChannel * 12;
        TUint32 count = IoU16(base + 8);

        if (aChannel == 3)
        {
            if (count == 0)
                count = 0x10000u;
        }
        else
        {
            count &= 0x3FFFu;

            if (count == 0)
                count = 0x4000u;
        }

        return count;
    }

    void LatchDma(TInt aChannel)
    {
        const TInt base = 0xB0 + aChannel * 12;

        TUint32 src = IoU32(base);
        TUint32 dst = IoU32(base + 4);

        src &= (aChannel == 0) ? 0x07FFFFFFu : 0x0FFFFFFFu;
        dst &= (aChannel == 3) ? 0x0FFFFFFFu : 0x07FFFFFFu;

        gDmaSrc[aChannel] = src;
        gDmaDst[aChannel] = dst;
        gDmaCount[aChannel] = DmaCountFromRegister(aChannel);
    }

    void RunDmaChannel(TInt aChannel)
    {
        const TInt base = 0xB0 + aChannel * 12;
        const TUint16 ctrl = IoU16(base + 10);

        const TInt dstCtl = (ctrl >> 5) & 3;
        const TInt srcCtl = (ctrl >> 7) & 3;
        const TBool word = (ctrl & (1u << 10)) != 0;
        const TBool repeat = (ctrl & (1u << 9)) != 0;
        const TInt timing = (ctrl >> 12) & 3;

        const TInt32 unit = word ? 4 : 2;

        TUint32 src = gDmaSrc[aChannel];
        TUint32 dst = gDmaDst[aChannel];
        const TUint32 count = gDmaCount[aChannel];

        src &= word ? ~3u : ~1u;
        dst &= word ? ~3u : ~1u;

        TUint32 loopCount = count;

        if (aChannel == 3 && !word && EepromDma(src, dst, count))
            loopCount = 0;

        for (TUint32 n = 0; n < loopCount; ++n)
        {
            if (word)
                NgbaMemoryWrite32(dst, NgbaMemoryRead32(src));
            else
                NgbaMemoryWrite16(dst, NgbaMemoryRead16(src));

            if (srcCtl == 1)
                src -= static_cast<TUint32>(unit);
            else if (srcCtl != 2)
                src += static_cast<TUint32>(unit);

            if (dstCtl == 1)
                dst -= static_cast<TUint32>(unit);
            else if (dstCtl == 0 || dstCtl == 3)
                dst += static_cast<TUint32>(unit);
        }

        gDmaSrc[aChannel] = src;
        gDmaDst[aChannel] = dst;

        if (ctrl & (1u << 14))
            NgbaMemoryRaiseIrq(static_cast<TUint16>(1u << (8 + aChannel)));

        if (repeat && timing != 0)
        {
            gDmaCount[aChannel] = DmaCountFromRegister(aChannel);

            if (dstCtl == 3)
            {
                TUint32 reload = IoU32(base + 4);
                reload &= (aChannel == 3) ? 0x0FFFFFFFu : 0x07FFFFFFu;
                gDmaDst[aChannel] = reload;
            }
        }
        else
        {
            gDmaEnabled[aChannel] = EFalse;
            gIo[base + 11] &= 0x7F;
        }
    }

    // Dipanggil saat byte tertinggi DMAxCNT_H ditulis (bit 15 = enable).
    void OnDmaControlWrite(TInt aChannel)
    {
        const TInt base = 0xB0 + aChannel * 12;
        const TBool enable = (gIo[base + 11] & 0x80) != 0;

        if (!enable)
        {
            gDmaEnabled[aChannel] = EFalse;
            return;
        }

        if (gDmaEnabled[aChannel])
            return;

        gDmaEnabled[aChannel] = ETrue;
        LatchDma(aChannel);

        const TInt timing = (IoU16(base + 10) >> 12) & 3;

        if (timing == 0)
            RunDmaChannel(aChannel);
    }

    // -----------------------------------------------------
    // TIMER
    // -----------------------------------------------------

    void OnTimerControlWrite(TInt aTimer)
    {
        const TInt base = 0x100 + aTimer * 4;
        const TBool enable = (gIo[base + 2] & 0x80) != 0;

        if (enable && !gTimerEnabled[aTimer])
        {
            gTimerCounter[aTimer] = IoU16(base);
            gTimerFrac[aTimer] = 0;
        }

        gTimerEnabled[aTimer] = enable;
    }

    // Baca 16/32-bit langsung dari ROM (RAM atau cache file) tanpa
    // memanggil ReadMemoryByte per byte. Mengembalikan EFalse jika harus
    // memakai jalur lambat (blok belum ada di cache, dsb).
    inline TBool RomFastRead(TUint32 aAddress, TInt aBytes, TUint32& aOut)
    {
        if (gRomSize <= 0)
            return EFalse;

        TUint32 off = aAddress - KRomBase;

        if (off >= static_cast<TUint32>(gRomSize))
            off %= static_cast<TUint32>(gRomSize);

        if (off + static_cast<TUint32>(aBytes) >
            static_cast<TUint32>(gRomSize))
            return EFalse;

        const TUint8* p;

        if (gRom != NULL)
        {
            p = gRom + off;
        }
        else
        {
            const TInt block = static_cast<TInt>(off) / KRomCacheSize;
            const TInt slot = block % KRomCacheSlots;
            const TInt inBlock = static_cast<TInt>(off) - block * KRomCacheSize;

            if (gRomCacheTag[slot] != block ||
                inBlock + aBytes > gRomCacheLen[slot])
                return EFalse;

            p = &gRomCache[slot][inBlock];
        }

        aOut = (aBytes == 2)
            ? static_cast<TUint32>(p[0] | (p[1] << 8))
            : (static_cast<TUint32>(p[0]) |
               (static_cast<TUint32>(p[1]) << 8) |
               (static_cast<TUint32>(p[2]) << 16) |
               (static_cast<TUint32>(p[3]) << 24));

        return ETrue;
    }

    TUint8 ReadMemoryByte(TUint32 aAddress)
    {
        if (!gReady)
            return 0;

        if (aAddress < 0x00004000u)
            return 0;

        if (aAddress >= KEwramBase &&
            aAddress < KEwramBase + 0x01000000u)
        {
            return ReadArray(
                gEwram,
                KEwramSize,
                aAddress - KEwramBase
            );
        }

        if (aAddress >= KIwramBase &&
            aAddress < KIwramBase + 0x01000000u)
        {
            return ReadArray(
                gIwram,
                KIwramSize,
                aAddress - KIwramBase
            );
        }

        if (aAddress >= KIoBase &&
            aAddress < KIoBase + KIoSize)
        {
            if (IsPpuIoAddress(aAddress))
                return NgbaPpuReadIo8(aAddress);

            const TUint32 ioOffset = aAddress - KIoBase;

            // TMxCNT_L: baca nilai counter berjalan.
            if (ioOffset >= 0x100u && ioOffset < 0x110u &&
                (ioOffset & 2u) == 0)
            {
                const TInt timer = static_cast<TInt>((ioOffset - 0x100u) >> 2);

                if (gTimerEnabled[timer])
                {
                    const TUint16 value =
                        static_cast<TUint16>(gTimerCounter[timer] & 0xFFFFu);

                    return (ioOffset & 1u)
                        ? static_cast<TUint8>(value >> 8)
                        : static_cast<TUint8>(value & 0xFFu);
                }
            }

            return gIo[ioOffset];
        }

        if (aAddress >= KPaletteBase &&
            aAddress < KPaletteBase + 0x01000000u)
        {
            return ReadArray(
                gPalette,
                KPaletteSize,
                aAddress - KPaletteBase
            );
        }

        if (aAddress >= KVramBase &&
            aAddress < KVramBase + 0x01000000u)
        {
            const TUint32 offset =
                MapVramOffset(aAddress - KVramBase);

            return gVram[offset];
        }

        if (aAddress >= KOamBase &&
            aAddress < KOamBase + 0x01000000u)
        {
            return ReadArray(
                gOam,
                KOamSize,
                aAddress - KOamBase
            );
        }

        if (aAddress >= KRomBase &&
            aAddress < 0x0E000000u)
        {
            // EEPROM selalu "siap" (bit 0 = 1) saat dipoll CPU.
            if ((aAddress >> 24) == 0x0Du && EepromActive())
                return (aAddress & 1u) ? 0 : 1;

            return ReadRom(aAddress);
        }

        if (aAddress >= KSramBase &&
            aAddress < KSramBase + 0x01000000u)
        {
            return ReadSave(aAddress - KSramBase);
        }

        return 0;
    }

    void WriteMemoryByte(
        TUint32 aAddress,
        TUint8 aValue
    )
    {
        if (!gReady)
            return;

        if (aAddress >= KEwramBase &&
            aAddress < KEwramBase + 0x01000000u)
        {
            WriteArray(
                gEwram,
                KEwramSize,
                aAddress - KEwramBase,
                aValue
            );
            return;
        }

        if (aAddress >= KIwramBase &&
            aAddress < KIwramBase + 0x01000000u)
        {
            WriteArray(
                gIwram,
                KIwramSize,
                aAddress - KIwramBase,
                aValue
            );
            return;
        }

        if (aAddress >= KIoBase &&
            aAddress < KIoBase + KIoSize)
        {
            if (IsPpuIoAddress(aAddress))
            {
                NgbaPpuWriteIo8(aAddress, aValue);
                return;
            }

            const TUint32 ioOffset = aAddress - KIoBase;

            // KEYINPUT hanya-baca.
            if (ioOffset == 0x130u || ioOffset == 0x131u)
                return;

            // REG_IF: Write-1-to-Clear.
            if (ioOffset == 0x202u || ioOffset == 0x203u)
            {
                gIo[ioOffset] &= static_cast<TUint8>(~aValue);
                return;
            }

            gIo[ioOffset] = aValue;

            // HALTCNT: bit 7 = 0 -> Halt sampai ada IRQ.
            // (bit 7 = 1 adalah mode Stop; diabaikan.)
            if (ioOffset == 0x301u)
            {
                if ((aValue & 0x80u) == 0)
                    gHaltRequest = ETrue;

                return;
            }

            // Timer: control byte rendah (0x102, 0x106, 0x10A, 0x10E).
            if (ioOffset >= 0x102u && ioOffset <= 0x10Eu &&
                ((ioOffset - 0x102u) & 3u) == 0)
            {
                OnTimerControlWrite(
                    static_cast<TInt>((ioOffset - 0x102u) >> 2)
                );
                return;
            }

            // DMA: byte tertinggi DMAxCNT_H (0xBB, 0xC7, 0xD3, 0xDF).
            if (ioOffset == 0xBBu || ioOffset == 0xC7u ||
                ioOffset == 0xD3u || ioOffset == 0xDFu)
            {
                OnDmaControlWrite(
                    static_cast<TInt>((ioOffset - 0xBBu) / 12u)
                );
                return;
            }

            return;
        }

        if (aAddress >= KPaletteBase &&
            aAddress < KPaletteBase + 0x01000000u)
        {
            WriteArray(
                gPalette,
                KPaletteSize,
                aAddress - KPaletteBase,
                aValue
            );
            return;
        }

        if (aAddress >= KVramBase &&
            aAddress < KVramBase + 0x01000000u)
        {
            const TUint32 offset =
                MapVramOffset(aAddress - KVramBase);

            gVram[offset] = aValue;
            return;
        }

        if (aAddress >= KOamBase &&
            aAddress < KOamBase + 0x01000000u)
        {
            WriteArray(
                gOam,
                KOamSize,
                aAddress - KOamBase,
                aValue
            );
            return;
        }

        if (aAddress >= KRomBase &&
            aAddress < 0x0E000000u)
            return;

        if (aAddress >= KSramBase &&
            aAddress < KSramBase + 0x01000000u)
        {
            WriteSave(aAddress - KSramBase, aValue);
        }
    }
}

TInt NgbaMemoryInit(
    const TUint8* aRom,
    TInt aRomSize
)
{
    if (aRom == NULL || aRomSize <= 0)
        return KErrArgument;

    if (aRomSize > 32 * 1024 * 1024)
        return KErrTooBig;

    NgbaMemoryShutdown();

    ClearMemory();

    gRom = aRom;
    gRomSize = aRomSize;

    FlushRomCache();

    gReady = ETrue;

    return KErrNone;
}

TInt NgbaMemoryInitFile(
    const TDesC& aPath,
    TInt aRomSize
)
{
    if (aRomSize <= 0 ||
        aRomSize > 32 * 1024 * 1024)
        return KErrArgument;

    NgbaMemoryShutdown();

    ClearMemory();

    TInt error = gRomFs.Connect();

    if (error != KErrNone)
        return error;

    error = gRomFile.Open(
        gRomFs,
        aPath,
        EFileRead | EFileShareReadersOnly
    );

    if (error != KErrNone)
    {
        gRomFs.Close();
        return error;
    }

    gRomFileOpen = ETrue;

    TInt actualSize = 0;

    error = gRomFile.Size(actualSize);

    if (error != KErrNone ||
        actualSize != aRomSize)
    {
        NgbaMemoryShutdown();

        if (error != KErrNone)
            return error;

        return KErrCorrupt;
    }

    gRom = NULL;
    gRomSize = aRomSize;

    FlushRomCache();

    // Tipe save (SRAM/Flash/EEPROM) dideteksi dari ROM, lalu muat file .sav.
    DetectSaveType();
    BuildSavePath(aPath);
    LoadSaveFile();

    gReady = ETrue;

    return KErrNone;
}

void NgbaMemorySetSaveType(TInt aType)
{
    if (aType >= ESaveSram && aType <= ESaveEeprom)
        gSaveType = aType;
}

void NgbaMemoryFlushSave()
{
    FlushSaveFile();
}

void NgbaMemoryShutdown()
{
    gReady = EFalse;

    FlushSaveFile();

    CloseRomFile();

    gRom = NULL;
    gRomSize = 0;

    FlushRomCache();

    ClearMemory();
}

// ---------------------------------------------------------
// BIOS HLE: SWI 01h - RegisterRamReset
// ---------------------------------------------------------

void NgbaMemoryRegisterRamReset(TUint32 aFlags)
{
    if (!gReady)
        return;

    if (aFlags & (1u << 0))
        Mem::FillZ(gEwram, sizeof(gEwram));

    if (aFlags & (1u << 1))
        Mem::FillZ(gIwram, sizeof(gIwram));

    if (aFlags & (1u << 2))
        Mem::FillZ(gPalette, sizeof(gPalette));

    if (aFlags & (1u << 3))
        Mem::FillZ(gVram, sizeof(gVram));

    if (aFlags & (1u << 4))
        Mem::FillZ(gOam, sizeof(gOam));

    if (aFlags & (1u << 5))
    {
        Mem::FillZ(
            &gIo[0x120],
            0x0C
        );
    }

    if (aFlags & (1u << 6))
    {
        Mem::FillZ(
            &gIo[0x060],
            0x48
        );
    }

    if (aFlags & (1u << 7))
    {
        Mem::FillZ(
            &gIo[0x000],
            0x060
        );

        Mem::FillZ(
            &gIo[0x0A8],
            0x078
        );

        Mem::FillZ(
            &gIo[0x134],
            0x2CC
        );
    }
}

TUint8 NgbaMemoryRead8(TUint32 aAddress)
{
    return ReadMemoryByte(aAddress);
}

TUint16 NgbaMemoryRead16(TUint32 aAddress)
{
    aAddress &= ~1u;

    if (IsPpuIoAddress(aAddress))
        return NgbaPpuReadIo16(aAddress);

    // Jalur cepat untuk RAM internal.
    if (gReady)
    {
        TUint32 o;

        switch (aAddress >> 24)
        {
        case 0x02:
            o = aAddress & (KEwramSize - 1);
            return static_cast<TUint16>(gEwram[o] | (gEwram[o + 1] << 8));

        case 0x03:
            o = aAddress & (KIwramSize - 1);
            return static_cast<TUint16>(gIwram[o] | (gIwram[o + 1] << 8));

        case 0x05:
            o = aAddress & (KPaletteSize - 1);
            return static_cast<TUint16>(gPalette[o] | (gPalette[o + 1] << 8));

        case 0x06:
            o = MapVramOffset(aAddress - KVramBase);
            return static_cast<TUint16>(gVram[o] | (gVram[o + 1] << 8));

        case 0x07:
            o = aAddress & (KOamSize - 1);
            return static_cast<TUint16>(gOam[o] | (gOam[o + 1] << 8));

        case 0x08:
        case 0x09:
        case 0x0A:
        case 0x0B:
        case 0x0C:
        {
            TUint32 v;

            if (RomFastRead(aAddress, 2, v))
                return static_cast<TUint16>(v);

            break;
        }

        default:
            break;
        }
    }

    const TUint16 b0 =
        ReadMemoryByte(aAddress);

    const TUint16 b1 =
        ReadMemoryByte(aAddress + 1);

    return static_cast<TUint16>(
        b0 | (b1 << 8)
    );
}

TUint32 NgbaMemoryRead32(TUint32 aAddress)
{
    aAddress &= ~3u;

    if (IsPpuIoAddress(aAddress))
        return NgbaPpuReadIo32(aAddress);

    // Jalur cepat untuk RAM internal.
    if (gReady)
    {
        TUint32 o;

        switch (aAddress >> 24)
        {
        case 0x02:
            o = aAddress & (KEwramSize - 1);
            return static_cast<TUint32>(gEwram[o]) |
                   (static_cast<TUint32>(gEwram[o + 1]) << 8) |
                   (static_cast<TUint32>(gEwram[o + 2]) << 16) |
                   (static_cast<TUint32>(gEwram[o + 3]) << 24);

        case 0x03:
            o = aAddress & (KIwramSize - 1);
            return static_cast<TUint32>(gIwram[o]) |
                   (static_cast<TUint32>(gIwram[o + 1]) << 8) |
                   (static_cast<TUint32>(gIwram[o + 2]) << 16) |
                   (static_cast<TUint32>(gIwram[o + 3]) << 24);

        case 0x08:
        case 0x09:
        case 0x0A:
        case 0x0B:
        case 0x0C:
        {
            TUint32 v;

            if (RomFastRead(aAddress, 4, v))
                return v;

            break;
        }

        default:
            break;
        }
    }

    const TUint32 b0 =
        ReadMemoryByte(aAddress);

    const TUint32 b1 =
        ReadMemoryByte(aAddress + 1);

    const TUint32 b2 =
        ReadMemoryByte(aAddress + 2);

    const TUint32 b3 =
        ReadMemoryByte(aAddress + 3);

    return b0 |
        (b1 << 8) |
        (b2 << 16) |
        (b3 << 24);
}

void NgbaMemoryWrite8(
    TUint32 aAddress,
    TUint8 aValue
)
{
    WriteMemoryByte(aAddress, aValue);
}

void NgbaMemoryWrite16(
    TUint32 aAddress,
    TUint16 aValue
)
{
    aAddress &= ~1u;

    if (IsPpuIoAddress(aAddress))
    {
        NgbaPpuWriteIo16(aAddress, aValue);
        return;
    }

    WriteMemoryByte(
        aAddress,
        static_cast<TUint8>(aValue)
    );

    WriteMemoryByte(
        aAddress + 1,
        static_cast<TUint8>(aValue >> 8)
    );
}

void NgbaMemoryWrite32(
    TUint32 aAddress,
    TUint32 aValue
)
{
    aAddress &= ~3u;

    if (IsPpuIoAddress(aAddress))
    {
        NgbaPpuWriteIo32(aAddress, aValue);
        return;
    }

    WriteMemoryByte(
        aAddress,
        static_cast<TUint8>(aValue)
    );

    WriteMemoryByte(
        aAddress + 1,
        static_cast<TUint8>(aValue >> 8)
    );

    WriteMemoryByte(
        aAddress + 2,
        static_cast<TUint8>(aValue >> 16)
    );

    WriteMemoryByte(
        aAddress + 3,
        static_cast<TUint8>(aValue >> 24)
    );
}

TBool NgbaMemoryReady()
{
    return gReady;
}

void NgbaMemoryRaiseIrq(TUint16 aFlag)
{
    if (!gReady)
        return;

    const TUint16 currentIf =
        static_cast<TUint16>(
            gIo[0x202] |
            (static_cast<TUint16>(gIo[0x203]) << 8)
        );

    const TUint16 updated =
        static_cast<TUint16>(currentIf | aFlag);

    gIo[0x202] =
        static_cast<TUint8>(updated & 0xFFu);

    gIo[0x203] =
        static_cast<TUint8>(updated >> 8);
}

// ---------------------------------------------------------
// KEYPAD, HALT, DMA TRIGGER, TIMER
// ---------------------------------------------------------

void NgbaMemorySetKeys(TUint16 aPressedMask)
{
    // KEYINPUT aktif-rendah: bit 0 = tombol ditekan.
    const TUint16 value =
        static_cast<TUint16>((~aPressedMask) & 0x03FFu);

    gIo[0x130] = static_cast<TUint8>(value & 0xFFu);
    gIo[0x131] = static_cast<TUint8>(value >> 8);
}

TBool NgbaMemoryTakeHaltRequest()
{
    const TBool request = gHaltRequest;
    gHaltRequest = EFalse;
    return request;
}

// aTiming: 1 = VBlank, 2 = HBlank.
void NgbaMemoryRunDma(TInt aTiming)
{
    if (!gReady)
        return;

    for (TInt i = 0; i < 4; ++i)
    {
        if (!gDmaEnabled[i])
            continue;

        const TInt timing = (IoU16(0xB0 + i * 12 + 10) >> 12) & 3;

        if (timing == aTiming)
            RunDmaChannel(i);
    }
}

void NgbaMemoryTickTimers(TInt aCycles)
{
    if (!gReady || aCycles <= 0)
        return;

    TBool overflowed = EFalse;

    for (TInt i = 0; i < 4; ++i)
    {
        if (!gTimerEnabled[i])
        {
            overflowed = EFalse;
            continue;
        }

        const TInt base = 0x100 + i * 4;
        const TUint16 control = IoU16(base + 2);

        TUint32 ticks = 0;

        if (i > 0 && (control & 0x0004u) != 0)
        {
            // Cascade: naik 1 setiap timer sebelumnya overflow.
            ticks = overflowed ? 1u : 0u;
        }
        else
        {
            const TUint32 div = static_cast<TUint32>(KTimerDiv[control & 3u]);
            gTimerFrac[i] += static_cast<TUint32>(aCycles);
            ticks = gTimerFrac[i] / div;
            gTimerFrac[i] -= ticks * div;
        }

        overflowed = EFalse;

        if (ticks == 0)
            continue;

        TUint32 counter = gTimerCounter[i] + ticks;

        if (counter >= 0x10000u)
        {
            const TUint32 reload = IoU16(base);
            const TUint32 period = 0x10000u - reload;

            overflowed = ETrue;

            if (control & 0x0040u)
                NgbaMemoryRaiseIrq(static_cast<TUint16>(1u << (3 + i)));

            counter = reload + ((counter - 0x10000u) % period);
        }

        gTimerCounter[i] = counter;
    }
}

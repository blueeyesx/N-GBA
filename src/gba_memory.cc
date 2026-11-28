
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
    TUint8 gSram[KSramSize];

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
        Mem::FillZ(gSram, sizeof(gSram));

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

        for (TUint32 n = 0; n < count; ++n)
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
            return ReadRom(aAddress);
        }

        if (aAddress >= KSramBase &&
            aAddress < KSramBase + 0x01000000u)
        {
            return ReadArray(
                gSram,
                KSramSize,
                aAddress - KSramBase
            );
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
            if (ioOffset == 0x301u)
            {
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
            WriteArray(
                gSram,
                KSramSize,
                aAddress - KSramBase,
                aValue
            );
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

    gReady = ETrue;

    return KErrNone;
}

void NgbaMemoryShutdown()
{
    gReady = EFalse;

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

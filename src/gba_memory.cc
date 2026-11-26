#include "gba_memory.h"

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
    const TInt KFlashSize = 0x20000; // 128 KB untuk Pokemon Emerald & FireRed

    const TInt KRomCacheSize = 4096;

    const TUint32 KEwramBase = 0x02000000u;
    const TUint32 KIwramBase = 0x03000000u;
    const TUint32 KIoBase = 0x04000000u;
    const TUint32 KPaletteBase = 0x05000000u;
    const TUint32 KVramBase = 0x06000000u;
    const TUint32 KOamBase = 0x07000000u;
    const TUint32 KRomBase = 0x08000000u;
    const TUint32 KSramBase = 0x0E000000u;

    enum TFlashState
    {
        EFlashNormal = 0,
        EFlashCmd1,
        EFlashCmd2,
        EFlashIdMode,
        EFlashEraseMode,
        EFlashWriteByte,
        EFlashBankSelect
    };

    TUint8 gEwram[KEwramSize];
    TUint8 gIwram[KIwramSize];
    TUint8 gIo[KIoSize];
    TUint8 gPalette[KPaletteSize];
    TUint8 gVram[KVramSize];
    TUint8 gOam[KOamSize];
    TUint8 gFlash[KFlashSize];

    TInt gFlashBank = 0;
    TInt gFlashState = EFlashNormal;

    const TUint8* gRom = NULL;

    RFs gRomFs;
    RFile gRomFile;
    TBool gRomFileOpen = EFalse;

    TUint8 gRomCache[KRomCacheSize];
    TInt gRomCacheStart = -1;
    TInt gRomCacheLength = 0;

    TInt gRomSize = 0;
    TBool gReady = EFalse;

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
        Mem::Fill(gFlash, sizeof(gFlash), 0xFF);

        gFlashBank = 0;
        gFlashState = EFlashNormal;

        // KEYINPUT (0x04000130) aktif-rendah: 0x03FF = semua tombol dilepas.
        gIo[0x130] = 0xFFu;
        gIo[0x131] = 0x03u;
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

        if (gRomCacheStart >= 0 &&
            romOffset >= gRomCacheStart &&
            romOffset < gRomCacheStart + gRomCacheLength)
        {
            return gRomCache[
                romOffset - gRomCacheStart
            ];
        }

        const TInt blockStart =
            (romOffset / KRomCacheSize) * KRomCacheSize;

        TInt bytesToRead =
            gRomSize - blockStart;

        if (bytesToRead > KRomCacheSize)
            bytesToRead = KRomCacheSize;

        if (bytesToRead <= 0)
            return 0;

        TPtr8 descriptor(
            gRomCache,
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
            gRomCacheStart = -1;
            gRomCacheLength = 0;
            return 0;
        }

        gRomCacheStart = blockStart;
        gRomCacheLength = descriptor.Length();

        const TInt cacheOffset =
            romOffset - gRomCacheStart;

        if (cacheOffset < 0 ||
            cacheOffset >= gRomCacheLength)
            return 0;

        return gRomCache[cacheOffset];
    }

    TUint8 ReadFlashByte(TUint32 aOffset)
    {
        const TUint32 offset = aOffset & 0xFFFFu;

        if (gFlashState == EFlashIdMode)
        {
            // Sanyo LE26FV10N1TS (128KB Flash): Manufacturer 0x62, Device 0x13
            if (offset == 0x0000u)
                return 0x62u;
            if (offset == 0x0001u)
                return 0x13u;
        }

        const TInt index = (gFlashBank * 0x10000) + static_cast<TInt>(offset);
        return gFlash[index];
    }

    void WriteFlashByte(TUint32 aOffset, TUint8 aValue)
    {
        const TUint32 offset = aOffset & 0xFFFFu;

        if (gFlashState == EFlashWriteByte)
        {
            const TInt index = (gFlashBank * 0x10000) + static_cast<TInt>(offset);
            gFlash[index] &= aValue;
            gFlashState = EFlashNormal;
            return;
        }

        if (gFlashState == EFlashBankSelect)
        {
            if (offset == 0x0000u)
                gFlashBank = (aValue & 1u) ? 1 : 0;

            gFlashState = EFlashNormal;
            return;
        }

        if (offset == 0x5555u && aValue == 0xAAu)
        {
            gFlashState = EFlashCmd1;
            return;
        }

        if (gFlashState == EFlashCmd1 && offset == 0x2AAAu && aValue == 0x55u)
        {
            gFlashState = EFlashCmd2;
            return;
        }

        if (gFlashState == EFlashCmd2)
        {
            if (offset == 0x5555u)
            {
                switch (aValue)
                {
                case 0x90u: // Enter ID mode
                    gFlashState = EFlashIdMode;
                    return;

                case 0xF0u: // Exit ID / Reset
                    gFlashState = EFlashNormal;
                    return;

                case 0x80u: // Erase command prefix
                    gFlashState = EFlashEraseMode;
                    return;

                case 0xA0u: // Byte program
                    gFlashState = EFlashWriteByte;
                    return;

                case 0xB0u: // Bank switch (128KB)
                    gFlashState = EFlashBankSelect;
                    return;
                }
            }

            gFlashState = EFlashNormal;
            return;
        }

        if (gFlashState == EFlashEraseMode)
        {
            if (offset == 0x5555u && aValue == 0x10u)
            {
                // Chip erase
                Mem::Fill(gFlash, sizeof(gFlash), 0xFF);
                gFlashState = EFlashNormal;
                return;
            }

            if (aValue == 0x30u)
            {
                // Sector erase (4 KB)
                const TInt sectorBase =
                    (gFlashBank * 0x10000) + static_cast<TInt>(offset & 0xF000u);
                Mem::Fill(&gFlash[sectorBase], 0x1000, 0xFF);
                gFlashState = EFlashNormal;
                return;
            }
        }

        if (aValue == 0xF0u)
            gFlashState = EFlashNormal;
    }

    void ExecuteImmediateDma(TInt aChannel)
    {
        const TInt base = 0x0B0 + aChannel * 12;

        TUint32 src =
            static_cast<TUint32>(gIo[base + 0]) |
            (static_cast<TUint32>(gIo[base + 1]) << 8) |
            (static_cast<TUint32>(gIo[base + 2]) << 16) |
            (static_cast<TUint32>(gIo[base + 3] & 0x0Fu) << 24);

        TUint32 dst =
            static_cast<TUint32>(gIo[base + 4]) |
            (static_cast<TUint32>(gIo[base + 5]) << 8) |
            (static_cast<TUint32>(gIo[base + 6]) << 16) |
            (static_cast<TUint32>(gIo[base + 7] & 0x0Fu) << 24);

        TUint32 count =
            static_cast<TUint32>(gIo[base + 8]) |
            (static_cast<TUint32>(gIo[base + 9]) << 8);

        const TUint16 control =
            static_cast<TUint16>(gIo[base + 10]) |
            (static_cast<TUint16>(gIo[base + 11]) << 8);

        if ((control & 0x8000u) == 0)
            return;

        const TInt timing = (control >> 12) & 3;
        if (timing != 0)
            return;

        if (count == 0)
            count = (aChannel == 3) ? 0x10000u : 0x4000u;

        const TBool wordTransfer = (control & 0x0400u) != 0;
        const TInt dstControl = (control >> 5) & 3;
        const TInt srcControl = (control >> 7) & 3;
        const TInt step = wordTransfer ? 4 : 2;

        for (TUint32 i = 0; i < count; ++i)
        {
            if (wordTransfer)
                NgbaMemoryWrite32(dst, NgbaMemoryRead32(src));
            else
                NgbaMemoryWrite16(dst, NgbaMemoryRead16(src));

            if (srcControl == 0)
                src += step;
            else if (srcControl == 1)
                src -= step;

            if (dstControl == 0 || dstControl == 3)
                dst += step;
            else if (dstControl == 1)
                dst -= step;
        }

        // Nonaktifkan bit DMA Enable (bit 15).
        gIo[base + 11] &= ~0x80u;

        // Picu DMA IRQ bila bit 14 aktif.
        if (control & 0x4000u)
        {
            gIo[0x203] |= static_cast<TUint8>(1u << aChannel);
        }
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
            return gIo[aAddress - KIoBase];
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
            return ReadFlashByte(aAddress - KSramBase);
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
            const TUint32 ioOffset = aAddress - KIoBase;

            // IF (0x04000202 - 0x04000203) adalah write-1-to-clear.
            if (ioOffset == 0x202u || ioOffset == 0x203u)
            {
                gIo[ioOffset] &= ~aValue;
                return;
            }

            // KEYINPUT (0x04000130 - 0x04000131) adalah read-only.
            if (ioOffset == 0x130u || ioOffset == 0x131u)
                return;

            gIo[ioOffset] = aValue;

            // Pemicu DMA immediate saat byte tinggi CNT_H ditulis.
            if (ioOffset == 0x0BBu)
                ExecuteImmediateDma(0);
            else if (ioOffset == 0x0C7u)
                ExecuteImmediateDma(1);
            else if (ioOffset == 0x0D3u)
                ExecuteImmediateDma(2);
            else if (ioOffset == 0x0DFu)
                ExecuteImmediateDma(3);

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
            WriteFlashByte(aAddress - KSramBase, aValue);
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

    gRomCacheStart = -1;
    gRomCacheLength = 0;

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

    gRomCacheStart = -1;
    gRomCacheLength = 0;

    gReady = ETrue;

    return KErrNone;
}

void NgbaMemoryShutdown()
{
    gReady = EFalse;

    CloseRomFile();

    gRom = NULL;
    gRomSize = 0;

    gRomCacheStart = -1;
    gRomCacheLength = 0;

    ClearMemory();
}

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

        gIo[0x130] = 0xFFu;
        gIo[0x131] = 0x03u;
    }
}

void NgbaMemoryAdvanceScanline()
{
    if (!gReady)
        return;

    TUint16 vcount = static_cast<TUint16>(
        gIo[0x06] | (gIo[0x07] << 8)
    );

    vcount = static_cast<TUint16>((vcount + 1u) % 228u);
    gIo[0x06] = static_cast<TUint8>(vcount & 0xFFu);
    gIo[0x07] = static_cast<TUint8>(vcount >> 8);

    TUint16 dispstat = static_cast<TUint16>(
        gIo[0x04] | (gIo[0x05] << 8)
    );

    dispstat &= ~0x0007u;

    if (vcount >= 160u && vcount < 227u)
    {
        dispstat |= 0x0001u; // VBlank flag

        if (vcount == 160u && (dispstat & 0x0008u))
            gIo[0x202] |= 0x01u; // VBlank IRQ
    }

    const TUint16 lyc = static_cast<TUint16>((dispstat >> 8) & 0xFFu);
    if (vcount == lyc)
    {
        dispstat |= 0x0004u; // VCounter match

        if (dispstat & 0x0020u)
            gIo[0x202] |= 0x04u; // VCounter IRQ
    }

    gIo[0x04] = static_cast<TUint8>(dispstat & 0xFFu);
    gIo[0x05] = static_cast<TUint8>(dispstat >> 8);
}

TUint8 NgbaMemoryRead8(TUint32 aAddress)
{
    return ReadMemoryByte(aAddress);
}

TUint16 NgbaMemoryRead16(TUint32 aAddress)
{
    aAddress &= ~1u;

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

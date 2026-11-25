
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
    const TInt KRomCacheSize = 4096;

    const TUint32 KEwramBase = 0x02000000u;
    const TUint32 KIwramBase = 0x03000000u;
    const TUint32 KIoBase = 0x04000000u;
    const TUint32 KPaletteBase = 0x05000000u;
    const TUint32 KVramBase = 0x06000000u;
    const TUint32 KOamBase = 0x07000000u;
    const TUint32 KRomBase = 0x08000000u;
    const TUint32 KSramBase = 0x0E000000u;

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

    TUint8 gRomCache[KRomCacheSize];
    TInt gRomCacheStart = -1;
    TInt gRomCacheLength = 0;

    TInt gRomSize = 0;
    TBool gReady = EFalse;

    TBool IsPpuIoAddress(TUint32 aAddress)
    {
        return (
            aAddress >= 0x04000000u &&
            aAddress <= 0x04000056u
        );
    }

    TBool IsPpuReadableRegister(TUint32 aAddress)
    {
        aAddress &= ~1u;

        if (aAddress >= 0x04000000u &&
            aAddress <= 0x0400000Eu)
            return ETrue;

        if (aAddress == 0x04000048u ||
            aAddress == 0x0400004Au ||
            aAddress == 0x04000050u ||
            aAddress == 0x04000052u)
            return ETrue;

        return EFalse;
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

        const TUint32 offset = aAddress - KRomBase;

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
            return gRomCache[romOffset - gRomCacheStart];
        }

        const TInt blockStart =
            (romOffset / KRomCacheSize) * KRomCacheSize;

        TInt bytesToRead = gRomSize - blockStart;

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

        const TInt cacheOffset = romOffset - gRomCacheStart;

        if (cacheOffset < 0 ||
            cacheOffset >= gRomCacheLength)
            return 0;

        return gRomCache[cacheOffset];
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
            const TUint32 aligned = aAddress & ~1u;

            if (IsPpuReadableRegister(aligned))
            {
                const TUint16 value =
                    NgbaPpuReadIo16(aligned);

                return (aAddress & 1u)
                    ? static_cast<TUint8>(value >> 8)
                    : static_cast<TUint8>(value);
            }

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
            const TUint32 offset = aAddress - KIoBase;

            gIo[offset] = aValue;

            if (IsPpuIoAddress(aAddress))
            {
                const TUint32 aligned = aAddress & ~1u;
                const TUint32 ioOffset = aligned - KIoBase;

                const TUint16 value =
                    static_cast<TUint16>(
                        gIo[ioOffset] |
                        (static_cast<TUint16>(
                            gIo[ioOffset + 1
                        ]) << 8)
                    );

                NgbaPpuWriteIo16(aligned, value);
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

    gRomCacheStart = -1;
    gRomCacheLength = 0;

    gReady = ETrue;

    NgbaPpuReset();

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

    NgbaPpuReset();

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
        Mem::FillZ(&gIo[0x120], 0x0C);

    if (aFlags & (1u << 6))
        Mem::FillZ(&gIo[0x060], 0x48);

    if (aFlags & (1u << 7))
    {
        Mem::FillZ(&gIo[0x000], 0x060);
        Mem::FillZ(&gIo[0x0A8], 0x078);
        Mem::FillZ(&gIo[0x134], 0x2CC);
    }

    NgbaPpuReset();
}

TUint8 NgbaMemoryRead8(TUint32 aAddress)
{
    return ReadMemoryByte(aAddress);
}

TUint16 NgbaMemoryRead16(TUint32 aAddress)
{
    aAddress &= ~1u;

    if (aAddress >= KIoBase &&
        aAddress < KIoBase + KIoSize &&
        IsPpuReadableRegister(aAddress))
    {
        return NgbaPpuReadIo16(aAddress);
    }

    const TUint16 b0 = ReadMemoryByte(aAddress);
    const TUint16 b1 = ReadMemoryByte(aAddress + 1);

    return static_cast<TUint16>(b0 | (b1 << 8));
}

TUint32 NgbaMemoryRead32(TUint32 aAddress)
{
    aAddress &= ~3u;

    const TUint32 b0 = ReadMemoryByte(aAddress);
    const TUint32 b1 = ReadMemoryByte(aAddress + 1);
    const TUint32 b2 = ReadMemoryByte(aAddress + 2);
    const TUint32 b3 = ReadMemoryByte(aAddress + 3);

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

    if (aAddress >= KIoBase &&
        aAddress < KIoBase + KIoSize)
    {
        const TUint32 offset = aAddress - KIoBase;

        gIo[offset] = static_cast<TUint8>(aValue);
        gIo[offset + 1] = static_cast<TUint8>(aValue >> 8);

        if (IsPpuIoAddress(aAddress))
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

    NgbaMemoryWrite16(
        aAddress,
        static_cast<TUint16>(aValue)
    );

    NgbaMemoryWrite16(
        aAddress + 2,
        static_cast<TUint16>(aValue >> 16)
    );
}

TBool NgbaMemoryReady()
{
    return gReady;
}

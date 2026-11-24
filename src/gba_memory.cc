
#include "gba_memory.h"

#include <e32std.h>

namespace
{
    // Ukuran wilayah memori GBA.
    const TInt KEwramSize = 0x40000;
    const TInt KIwramSize = 0x8000;
    const TInt KIoSize = 0x400;
    const TInt KPaletteSize = 0x400;
    const TInt KVramSize = 0x18000;
    const TInt KOamSize = 0x400;
    const TInt KSramSize = 0x10000;

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
    TInt gRomSize = 0;
    TBool gReady = EFalse;

    TUint8 ReadArray(
        const TUint8* aData,
        TInt aSize,
        TUint32 aOffset
    )
    {
        if (aData == NULL || aSize <= 0)
            return 0;

        return aData[aOffset % static_cast<TUint32>(aSize)];
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

        aData[aOffset % static_cast<TUint32>(aSize)] = aValue;
    }

    TUint32 MapVramOffset(TUint32 offset)
    {
        // VRAM memiliki area 96 KB.
        // Bagian 0x18000-0x1FFFF mencerminkan
        // bagian 0x10000-0x17FFF.
        offset &= 0x1FFFFu;

        if (offset >= 0x18000u)
            offset -= 0x8000u;

        return offset;
    }

    TUint8 ReadRom(TUint32 address)
    {
        if (gRom == NULL || gRomSize <= 0)
            return 0;

        const TUint32 offset = address - KRomBase;

        // ROM GBA berada pada beberapa jendela alamat.
        const TUint32 romOffset =
            offset % static_cast<TUint32>(gRomSize);

        return gRom[romOffset];
    }

    TUint8 ReadMemoryByte(TUint32 address)
    {
        if (!gReady)
            return 0;

        // BIOS GBA: 16 KB.
        // Belum ada image BIOS/HLE.
        if (address < 0x00004000u)
            return 0;

        // EWRAM: 256 KB.
        if (address >= KEwramBase &&
            address < KEwramBase + 0x01000000u)
        {
            return ReadArray(
                gEwram,
                KEwramSize,
                address - KEwramBase
            );
        }

        // IWRAM: 32 KB.
        if (address >= KIwramBase &&
            address < KIwramBase + 0x01000000u)
        {
            return ReadArray(
                gIwram,
                KIwramSize,
                address - KIwramBase
            );
        }

        // I/O registers.
        if (address >= KIoBase &&
            address < KIoBase + KIoSize)
        {
            return gIo[address - KIoBase];
        }

        // Palette RAM.
        if (address >= KPaletteBase &&
            address < KPaletteBase + 0x01000000u)
        {
            return ReadArray(
                gPalette,
                KPaletteSize,
                address - KPaletteBase
            );
        }

        // Video RAM.
        if (address >= KVramBase &&
            address < KVramBase + 0x01000000u)
        {
            const TUint32 offset =
                MapVramOffset(address - KVramBase);

            return gVram[offset];
        }

        // OAM.
        if (address >= KOamBase &&
            address < KOamBase + 0x01000000u)
        {
            return ReadArray(
                gOam,
                KOamSize,
                address - KOamBase
            );
        }

        // Game Pak ROM.
        if (address >= KRomBase &&
            address < 0x0E000000u)
        {
            return ReadRom(address);
        }

        // SRAM / Flash area.
        if (address >= KSramBase &&
            address < KSramBase + 0x01000000u)
        {
            return ReadArray(
                gSram,
                KSramSize,
                address - KSramBase
            );
        }

        return 0;
    }

    void WriteMemoryByte(
        TUint32 address,
        TUint8 value
    )
    {
        if (!gReady)
            return;

        // EWRAM.
        if (address >= KEwramBase &&
            address < KEwramBase + 0x01000000u)
        {
            WriteArray(
                gEwram,
                KEwramSize,
                address - KEwramBase,
                value
            );
            return;
        }

        // IWRAM.
        if (address >= KIwramBase &&
            address < KIwramBase + 0x01000000u)
        {
            WriteArray(
                gIwram,
                KIwramSize,
                address - KIwramBase,
                value
            );
            return;
        }

        // I/O registers.
        if (address >= KIoBase &&
            address < KIoBase + KIoSize)
        {
            gIo[address - KIoBase] = value;
            return;
        }

        // Palette RAM.
        if (address >= KPaletteBase &&
            address < KPaletteBase + 0x01000000u)
        {
            WriteArray(
                gPalette,
                KPaletteSize,
                address - KPaletteBase,
                value
            );
            return;
        }

        // Video RAM.
        if (address >= KVramBase &&
            address < KVramBase + 0x01000000u)
        {
            const TUint32 offset =
                MapVramOffset(address - KVramBase);

            gVram[offset] = value;
            return;
        }

        // OAM.
        if (address >= KOamBase &&
            address < KOamBase + 0x01000000u)
        {
            WriteArray(
                gOam,
                KOamSize,
                address - KOamBase,
                value
            );
            return;
        }

        // ROM tidak dapat ditulis.
        if (address >= KRomBase &&
            address < 0x0E000000u)
        {
            return;
        }

        // SRAM.
        if (address >= KSramBase &&
            address < KSramBase + 0x01000000u)
        {
            WriteArray(
                gSram,
                KSramSize,
                address - KSramBase,
                value
            );
        }
    }
}

TInt NgbaMemoryInit(
    const TUint8* rom,
    TInt romSize
)
{
    if (rom == NULL || romSize <= 0)
        return KErrArgument;

    if (romSize > 32 * 1024 * 1024)
        return KErrTooBig;

    Mem::FillZ(gEwram, sizeof(gEwram));
    Mem::FillZ(gIwram, sizeof(gIwram));
    Mem::FillZ(gIo, sizeof(gIo));
    Mem::FillZ(gPalette, sizeof(gPalette));
    Mem::FillZ(gVram, sizeof(gVram));
    Mem::FillZ(gOam, sizeof(gOam));
    Mem::FillZ(gSram, sizeof(gSram));

    gRom = rom;
    gRomSize = romSize;
    gReady = ETrue;

    return KErrNone;
}

void NgbaMemoryShutdown()
{
    gReady = EFalse;
    gRom = NULL;
    gRomSize = 0;

    Mem::FillZ(gEwram, sizeof(gEwram));
    Mem::FillZ(gIwram, sizeof(gIwram));
    Mem::FillZ(gIo, sizeof(gIo));
    Mem::FillZ(gPalette, sizeof(gPalette));
    Mem::FillZ(gVram, sizeof(gVram));
    Mem::FillZ(gOam, sizeof(gOam));
    Mem::FillZ(gSram, sizeof(gSram));
}

TUint8 NgbaMemoryRead8(TUint32 address)
{
    return ReadMemoryByte(address);
}

TUint16 NgbaMemoryRead16(TUint32 address)
{
    address &= ~1u;

    const TUint16 low =
        static_cast<TUint16>(ReadMemoryByte(address));

    const TUint16 high =
        static_cast<TUint16>(
            ReadMemoryByte(address + 1)
        );

    return static_cast<TUint16>(
        low | (high << 8)
    );
}

TUint32 NgbaMemoryRead32(TUint32 address)
{
    address &= ~3u;

    const TUint32 b0 = ReadMemoryByte(address);
    const TUint32 b1 = ReadMemoryByte(address + 1);
    const TUint32 b2 = ReadMemoryByte(address + 2);
    const TUint32 b3 = ReadMemoryByte(address + 3);

    return
        b0 |
        (b1 << 8) |
        (b2 << 16) |
        (b3 << 24);
}

void NgbaMemoryWrite8(
    TUint32 address,
    TUint8 value
)
{
    WriteMemoryByte(address, value);
}

void NgbaMemoryWrite16(
    TUint32 address,
    TUint16 value
)
{
    address &= ~1u;

    WriteMemoryByte(
        address,
        static_cast<TUint8>(value & 0xFF)
    );

    WriteMemoryByte(
        address + 1,
        static_cast<TUint8>((value >> 8) & 0xFF)
    );
}

void NgbaMemoryWrite32(
    TUint32 address,
    TUint32 value
)
{
    address &= ~3u;

    WriteMemoryByte(
        address,
        static_cast<TUint8>(value & 0xFF)
    );

    WriteMemoryByte(
        address + 1,
        static_cast<TUint8>((value >> 8) & 0xFF)
    );

    WriteMemoryByte(
        address + 2,
        static_cast<TUint8>((value >> 16) & 0xFF)
    );

    WriteMemoryByte(
        address + 3,
        static_cast<TUint8>((value >> 24) & 0xFF)
    );
}

TBool NgbaMemoryReady()
{
    return gReady;
}

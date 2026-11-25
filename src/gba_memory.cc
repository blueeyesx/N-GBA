#include "gba_memory.h"
#include "gba_ppu.h"

#include <e32std.h>
#include <f32file.h>

namespace
{
    const TInt KBiosSize     = 0x4000;
    const TInt KEwramSize    = 0x40000;
    const TInt KIwramSize    = 0x8000;
    const TInt KIoSize       = 0x400;
    const TInt KPaletteSize  = 0x400;
    const TInt KVramSize     = 0x18000;
    const TInt KOamSize      = 0x400;
    const TInt KFlashSize    = 0x20000; // 128KB (2 bank x 64KB) untuk Pokemon Emerald/FireRed
    const TInt KRomCacheSize = 4096;

    const TUint32 KEwramBase   = 0x02000000u;
    const TUint32 KIwramBase   = 0x03000000u;
    const TUint32 KIoBase      = 0x04000000u;
    const TUint32 KPaletteBase = 0x05000000u;
    const TUint32 KVramBase    = 0x06000000u;
    const TUint32 KOamBase     = 0x07000000u;
    const TUint32 KRomBase     = 0x08000000u;
    const TUint32 KSramBase    = 0x0E000000u;

    // Register I/O penting.
    const TUint32 KRegKeyInput    = 0x04000130u;
    const TUint32 KRegIf          = 0x04000202u;
    const TUint16 KKeyAllReleased = 0x03FFu;

    TUint8 gBios[KBiosSize];
    TUint8 gEwram[KEwramSize];
    TUint8 gIwram[KIwramSize];
    TUint8 gIo[KIoSize];
    TUint8 gPalette[KPaletteSize];
    TUint8 gVram[KVramSize];
    TUint8 gOam[KOamSize];
    TUint8 gFlash[KFlashSize];

    const TUint8* gRom = NULL;

    RFs gRomFs;
    RFile gRomFile;
    TBool gRomFileOpen = EFalse;

    TUint8 gRomCache[KRomCacheSize];
    TInt gRomCacheStart = -1;
    TInt gRomCacheLength = 0;

    TInt gRomSize = 0;
    TBool gReady = EFalse;

    // ---------------------------------------------------------
    // STATE FLASH 128KB (Sanyo 0x1362 - kompatibel Pokemon/GBA)
    // ---------------------------------------------------------
    enum TFlashState
    {
        EFlashReady = 0,
        EFlashCmd1,
        EFlashCmd2,
        EFlashIdMode,
        EFlashEraseCmd1,
        EFlashEraseCmd2,
        EFlashWriteByte,
        EFlashBankSelect
    };

    TFlashState gFlashState = EFlashReady;
    TUint8 gFlashBank = 0;

    void ResetFlashState()
    {
        gFlashState = EFlashReady;
        gFlashBank = 0;
        Mem::Fill(gFlash, sizeof(gFlash), 0xFF);
    }

    TUint8 ReadFlashByte(TUint32 aOffset)
    {
        const TUint32 addr = aOffset & 0xFFFFu;

        if (gFlashState == EFlashIdMode)
        {
            if (addr == 0x0000u)
                return 0x62u; // Sanyo Manufacturer ID
            if (addr == 0x0001u)
                return 0x13u; // Sanyo 128KB Device ID
            return 0xFFu;
        }

        const TUint32 fullOffset =
            (static_cast<TUint32>(gFlashBank & 1u) << 16) | addr;
        return gFlash[fullOffset];
    }

    void WriteFlashByte(TUint32 aOffset, TUint8 aValue)
    {
        const TUint32 addr = aOffset & 0xFFFFu;

        switch (gFlashState)
        {
        case EFlashReady:
            if (addr == 0x5555u && aValue == 0xAAu)
                gFlashState = EFlashCmd1;
            break;

        case EFlashCmd1:
            if (addr == 0x2AAAu && aValue == 0x55u)
                gFlashState = EFlashCmd2;
            else
                gFlashState = EFlashReady;
            break;

        case EFlashCmd2:
            if (addr == 0x5555u)
            {
                if (aValue == 0x90u)
                    gFlashState = EFlashIdMode;
                else if (aValue == 0x80u)
                    gFlashState = EFlashEraseCmd1;
                else if (aValue == 0xA0u)
                    gFlashState = EFlashWriteByte;
                else if (aValue == 0xB0u)
                    gFlashState = EFlashBankSelect;
                else
                    gFlashState = EFlashReady;
            }
            else
            {
                gFlashState = EFlashReady;
            }
            break;

        case EFlashIdMode:
            if (addr == 0x5555u && aValue == 0xAAu)
                gFlashState = EFlashCmd1;
            else if (aValue == 0xF0u)
                gFlashState = EFlashReady;
            break;

        case EFlashEraseCmd1:
            if (addr == 0x5555u && aValue == 0xAAu)
                gFlashState = EFlashEraseCmd2;
            else
                gFlashState = EFlashReady;
            break;

        case EFlashEraseCmd2:
            if (addr == 0x2AAAu && aValue == 0x55u)
            {
                // Tunggu perintah erase chip (0x10) atau sektor (0x30).
            }
            else if (addr == 0x5555u && aValue == 0x10u)
            {
                Mem::Fill(gFlash, sizeof(gFlash), 0xFF);
                gFlashState = EFlashReady;
            }
            else if (aValue == 0x30u)
            {
                const TUint32 sectorBase =
                    ((static_cast<TUint32>(gFlashBank & 1u) << 16) |
                     (addr & 0xF000u));
                Mem::Fill(&gFlash[sectorBase], 0x1000, 0xFF);
                gFlashState = EFlashReady;
            }
            else
            {
                gFlashState = EFlashReady;
            }
            break;

        case EFlashWriteByte:
        {
            const TUint32 fullOffset =
                (static_cast<TUint32>(gFlashBank & 1u) << 16) | addr;
            gFlash[fullOffset] &= aValue;
            gFlashState = EFlashReady;
            break;
        }

        case EFlashBankSelect:
            if (addr == 0x0000u)
                gFlashBank = aValue & 1u;
            gFlashState = EFlashReady;
            break;

        default:
            gFlashState = EFlashReady;
            break;
        }
    }

    // ---------------------------------------------------------
    // DMA CONTROLLER (DMA0 - DMA3: 0x040000B0 - 0x040000DF)
    // ---------------------------------------------------------
    struct TDmaChannel
    {
        TUint32 sadReg;
        TUint32 dadReg;
        TUint16 countReg;
        TUint16 controlReg;

        TUint32 internalSad;
        TUint32 internalDad;
        TUint32 internalCount;
    };

    TDmaChannel gDma[4];

    void ResetDma()
    {
        for (TInt i = 0; i < 4; ++i)
        {
            gDma[i].sadReg = 0;
            gDma[i].dadReg = 0;
            gDma[i].countReg = 0;
            gDma[i].controlReg = 0;
            gDma[i].internalSad = 0;
            gDma[i].internalDad = 0;
            gDma[i].internalCount = 0;
        }
    }

    TUint32 MaskDmaSource(TInt aChannel, TUint32 aAddress)
    {
        return (aChannel == 0)
            ? (aAddress & 0x07FFFFFFu)
            : (aAddress & 0x0FFFFFFFu);
    }

    TUint32 MaskDmaDest(TInt aChannel, TUint32 aAddress)
    {
        return (aChannel == 3)
            ? (aAddress & 0x0FFFFFFFu)
            : (aAddress & 0x07FFFFFFu);
    }

    void ExecuteDmaChannel(TInt aChannel)
    {
        TDmaChannel& ch = gDma[aChannel];

        if ((ch.controlReg & 0x8000u) == 0)
            return;

        const TInt destCtrl = (ch.controlReg >> 5) & 3;
        const TInt srcCtrl  = (ch.controlReg >> 7) & 3;
        const TBool repeat  = (ch.controlReg & (1u << 9)) != 0;
        const TBool word32  = (ch.controlReg & (1u << 10)) != 0;
        const TInt timing   = (ch.controlReg >> 12) & 3;
        const TBool irqEn   = (ch.controlReg & (1u << 14)) != 0;

        const TUint32 step = word32 ? 4u : 2u;
        TUint32 count = ch.internalCount;

        if (count == 0)
            count = (aChannel == 3) ? 0x10000u : 0x4000u;

        TUint32 src = ch.internalSad & ~(step - 1u);
        TUint32 dst = ch.internalDad & ~(step - 1u);

        for (TUint32 i = 0; i < count; ++i)
        {
            if (word32)
            {
                const TUint32 val = NgbaMemoryRead32(src);
                NgbaMemoryWrite32(dst, val);
            }
            else
            {
                const TUint16 val = NgbaMemoryRead16(src);
                NgbaMemoryWrite16(dst, val);
            }

            if (srcCtrl == 0)
                src += step;
            else if (srcCtrl == 1)
                src -= step;

            if (destCtrl == 0 || destCtrl == 3)
                dst += step;
            else if (destCtrl == 1)
                dst -= step;
        }

        ch.internalSad = src;
        ch.internalDad = dst;

        if (irqEn)
            NgbaMemoryRaiseIrq(static_cast<TUint16>(1u << (8 + aChannel)));

        if (repeat && timing != 0)
        {
            ch.internalCount = ch.countReg;

            if (destCtrl == 3)
                ch.internalDad = MaskDmaDest(aChannel, ch.dadReg);
        }
        else
        {
            ch.controlReg &= ~0x8000u;
            const TUint32 cntOffset =
                static_cast<TUint32>(0xB0 + aChannel * 12 + 10);
            gIo[cntOffset] = static_cast<TUint8>(ch.controlReg);
            gIo[cntOffset + 1] = static_cast<TUint8>(ch.controlReg >> 8);
        }
    }

    void TriggerDmaByTiming(TInt aTiming)
    {
        for (TInt i = 0; i < 4; ++i)
        {
            if ((gDma[i].controlReg & 0x8000u) == 0)
                continue;

            const TInt timing = (gDma[i].controlReg >> 12) & 3;
            if (timing == aTiming)
                ExecuteDmaChannel(i);
        }
    }

    // ---------------------------------------------------------
    // TIMERS 0..3 (0x04000100 - 0x0400010F) & SERIAL SIO (0x04000120 - 0x0400012A)
    // ---------------------------------------------------------
    struct TGbaTimer
    {
        TUint16 reload;
        TUint16 counter;
        TUint16 control;
        TUint32 subCycles;
    };

    TGbaTimer gTimers[4];

    void ResetTimers()
    {
        for (TInt i = 0; i < 4; ++i)
        {
            gTimers[i].reload = 0;
            gTimers[i].counter = 0;
            gTimers[i].control = 0;
            gTimers[i].subCycles = 0;
        }
    }

    TUint32 GetTimerPrescaler(TUint16 aControl)
    {
        switch (aControl & 3u)
        {
        case 0: return 1u;
        case 1: return 64u;
        case 2: return 256u;
        case 3:
        default: return 1024u;
        }
    }

    void OverflowTimer(TInt aIndex)
    {
        if (aIndex < 0 || aIndex >= 4)
            return;

        TGbaTimer& t = gTimers[aIndex];
        t.counter = t.reload;

        // Bit 6 (0x40) = Timer IRQ Enable -> IRQ bit (3 + aIndex)
        if ((t.control & 0x0040u) != 0)
        {
            NgbaMemoryRaiseIrq(static_cast<TUint16>(1u << (3 + aIndex)));
        }

        // Cascade ke timer berikutnya jika bit 2 (0x04 = Count-up timing) aktif
        if (aIndex + 1 < 4)
        {
            TGbaTimer& next = gTimers[aIndex + 1];
            if ((next.control & 0x0080u) != 0 && (next.control & 0x0004u) != 0)
            {
                if (next.counter == 0xFFFFu)
                {
                    OverflowTimer(aIndex + 1);
                }
                else
                {
                    ++next.counter;
                }
            }
        }
    }

    void StepTimers(TUint32 aCycles)
    {
        if (!gReady || aCycles == 0)
            return;

        for (TInt i = 0; i < 4; ++i)
        {
            TGbaTimer& t = gTimers[i];
            if ((t.control & 0x0080u) == 0)
                continue;

            // Mode count-up (bit 2 = 1) dihitung dari overflow timer sebelumnya
            if ((t.control & 0x0004u) != 0)
                continue;

            const TUint32 prescaler = GetTimerPrescaler(t.control);
            t.subCycles += aCycles;
            const TUint32 ticks = t.subCycles / prescaler;
            t.subCycles %= prescaler;

            if (ticks == 0)
                continue;

            const TUint32 remaining = 0x10000u - static_cast<TUint32>(t.counter);
            if (ticks >= remaining)
            {
                TUint32 remTicks = ticks - remaining;
                OverflowTimer(i);
                const TUint32 span = (t.reload == 0)
                    ? 0x10000u
                    : (0x10000u - static_cast<TUint32>(t.reload));
                if (span > 0 && remTicks >= span)
                {
                    remTicks %= span;
                }
                t.counter = static_cast<TUint16>(static_cast<TUint32>(t.counter) + remTicks);
            }
            else
            {
                t.counter = static_cast<TUint16>(static_cast<TUint32>(t.counter) + ticks);
            }
        }
    }

    void SyncTimerIoRegisters()
    {
        for (TInt i = 0; i < 4; ++i)
        {
            const TUint32 baseOff = static_cast<TUint32>(0x100 + i * 4);
            gIo[baseOff + 0] = static_cast<TUint8>(gTimers[i].counter & 0xFFu);
            gIo[baseOff + 1] = static_cast<TUint8>((gTimers[i].counter >> 8) & 0xFFu);
            gIo[baseOff + 2] = static_cast<TUint8>(gTimers[i].control & 0xFFu);
            gIo[baseOff + 3] = static_cast<TUint8>((gTimers[i].control >> 8) & 0xFFu);
        }
    }

    void WriteTimerIo16(TUint32 aAddress, TUint16 aValue)
    {
        const TUint32 rel = aAddress - 0x04000100u;
        const TInt idx = static_cast<TInt>(rel / 4u);
        const TUint32 regOff = rel % 4u;
        if (idx < 0 || idx >= 4)
            return;

        TGbaTimer& t = gTimers[idx];
        if (regOff == 0u)
        {
            t.reload = aValue;
            gIo[0x100 + idx * 4 + 0] = static_cast<TUint8>(aValue & 0xFFu);
            gIo[0x100 + idx * 4 + 1] = static_cast<TUint8>((aValue >> 8) & 0xFFu);
        }
        else if (regOff == 2u)
        {
            const TBool wasEnabled = (t.control & 0x0080u) != 0;
            const TBool nowEnabled = (aValue & 0x0080u) != 0;
            t.control = aValue;
            if (!wasEnabled && nowEnabled)
            {
                t.counter = t.reload;
                t.subCycles = 0;
            }
            SyncTimerIoRegisters();
        }
    }

    void HandleSerialWrite16(TUint32 aAddress, TUint16 aValue)
    {
        const TUint32 offset = aAddress - KIoBase;
        gIo[offset + 0] = static_cast<TUint8>(aValue & 0xFFu);
        gIo[offset + 1] = static_cast<TUint8>((aValue >> 8) & 0xFFu);

        // SIOCNT (0x04000128): jika bit 7 (Start/Busy = 0x80) diset, selesaikan segera
        // dengan data 0xFFFFFFFF (tidak ada link/wireless terhubung) agar game tidak menggantung.
        if (aAddress == 0x04000128u && (aValue & 0x0080u) != 0)
        {
            gIo[0x120] = 0xFFu;
            gIo[0x121] = 0xFFu;
            gIo[0x122] = 0xFFu;
            gIo[0x123] = 0xFFu;

            const TUint16 clearedBusy = static_cast<TUint16>(aValue & ~0x0080u);
            gIo[0x128] = static_cast<TUint8>(clearedBusy & 0xFFu);
            gIo[0x129] = static_cast<TUint8>((clearedBusy >> 8) & 0xFFu);

            if ((aValue & 0x4000u) != 0)
            {
                NgbaMemoryRaiseIrq(0x0080u);
            }
        }
    }

    void OnPpuVBlank()
    {
        StepTimers(1232u);
        SyncTimerIoRegisters();
        TriggerDmaByTiming(1);
    }

    void OnPpuHBlank()
    {
        StepTimers(1232u);
        SyncTimerIoRegisters();
        TriggerDmaByTiming(2);
    }

    TBool IsDmaIoAddress(TUint32 aAddress)
    {
        return (aAddress >= 0x040000B0u && aAddress < 0x040000E0u);
    }

    void WriteDmaIo16(TUint32 aAddress, TUint16 aValue)
    {
        const TUint32 offset = aAddress - 0x040000B0u;
        const TInt channel = static_cast<TInt>(offset / 12u);
        const TUint32 regOffset = offset % 12u;
        TDmaChannel& ch = gDma[channel];

        switch (regOffset)
        {
        case 0x0u:
            ch.sadReg = (ch.sadReg & 0xFFFF0000u) | aValue;
            break;
        case 0x2u:
            ch.sadReg = (ch.sadReg & 0x0000FFFFu) |
                        (static_cast<TUint32>(aValue) << 16);
            break;
        case 0x4u:
            ch.dadReg = (ch.dadReg & 0xFFFF0000u) | aValue;
            break;
        case 0x6u:
            ch.dadReg = (ch.dadReg & 0x0000FFFFu) |
                        (static_cast<TUint32>(aValue) << 16);
            break;
        case 0x8u:
            ch.countReg = aValue;
            break;
        case 0xAu:
        {
            const TBool wasEnabled = (ch.controlReg & 0x8000u) != 0;
            const TBool nowEnabled = (aValue & 0x8000u) != 0;

            ch.controlReg = aValue;

            if (!wasEnabled && nowEnabled)
            {
                ch.internalSad = MaskDmaSource(channel, ch.sadReg);
                ch.internalDad = MaskDmaDest(channel, ch.dadReg);
                ch.internalCount = ch.countReg;

                const TInt timing = (aValue >> 12) & 3;
                if (timing == 0)
                    ExecuteDmaChannel(channel);
            }
            break;
        }
        default:
            break;
        }
    }

    // ---------------------------------------------------------
    // VEKTOR IRQ BIOS GBA (0x00000018 -> 0x00000128 -> [0x03007FFC])
    // ---------------------------------------------------------
    void WriteBiosWord(TUint32 aOffset, TUint32 aWord)
    {
        gBios[aOffset + 0] = static_cast<TUint8>(aWord & 0xFFu);
        gBios[aOffset + 1] = static_cast<TUint8>((aWord >> 8) & 0xFFu);
        gBios[aOffset + 2] = static_cast<TUint8>((aWord >> 16) & 0xFFu);
        gBios[aOffset + 3] = static_cast<TUint8>((aWord >> 24) & 0xFFu);
    }

    void InitBiosStub()
    {
        Mem::FillZ(gBios, sizeof(gBios));

        // Vektor exception awal (Reset / Undef / SWI / Prefetch / Data Abort):
        // Kembali ke LR / loop aman tanpa jatuh ke vektor IRQ (0x00000018) secara tidak sengaja.
        // MOVS PC, LR (0xE1B0F00E) pada 0x00000004, 0x0000000C, 0x00000010, 0x00000014
        WriteBiosWord(0x0004u, 0xE1B0F00Eu);
        WriteBiosWord(0x000Cu, 0xE1B0F00Eu);
        WriteBiosWord(0x0010u, 0xE1B0F00Eu);
        WriteBiosWord(0x0014u, 0xE1B0F00Eu);

        // Vektor IRQ hardware di 0x00000018: B 0x00000128
        WriteBiosWord(0x0018u, 0xEA000042u);

        // Rutinitas IRQ BIOS GBA di 0x00000128:
        // STMFD SP!, {R0-R3, R12, LR}
        WriteBiosWord(0x0128u, 0xE92D500Fu);
        // MOV R0, #0x03000000 (0x30 ROR 8 -> rotate_imm=4 -> 0xE3A00430)
        WriteBiosWord(0x012Cu, 0xE3A00430u);
        // ADD R0, R0, #0x7F00 (0x7F ROR 24 -> rotate_imm=12 -> 0xE2800C7F) -> R0 = 0x03007F00
        WriteBiosWord(0x0130u, 0xE2800C7Fu);
        // ADD LR, PC, #0  (LR = 0x0000013C)
        WriteBiosWord(0x0134u, 0xE28FE000u);
        // LDR PC, [R0, #0xFC] (lompat ke vektor IRQ game di 0x03007FFC)
        WriteBiosWord(0x0138u, 0xE590F0FCu);
        // LDMFD SP!, {R0-R3, R12, LR}
        WriteBiosWord(0x013Cu, 0xE8BD500Fu);
        // SUBS PC, LR, #4
        WriteBiosWord(0x0140u, 0xE25EF004u);
    }

    void BindPpuCallbacks()
    {
        NgbaPpuSetIrqCallback(&NgbaMemoryRaiseIrq);
        NgbaPpuSetVBlankCallback(&OnPpuVBlank);
        NgbaPpuSetHBlankCallback(&OnPpuHBlank);
    }

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

    void SetDefaultKeyInput()
    {
        // KEYINPUT (0x04000130) bersifat active-low: 0x03FF = semua tombol dilepas.
        gIo[0x130] = static_cast<TUint8>(KKeyAllReleased & 0xFFu);
        gIo[0x131] = static_cast<TUint8>((KKeyAllReleased >> 8) & 0xFFu);
    }

    void ClearMemory()
    {
        InitBiosStub();
        Mem::FillZ(gEwram, sizeof(gEwram));
        Mem::FillZ(gIwram, sizeof(gIwram));
        Mem::FillZ(gIo, sizeof(gIo));
        Mem::FillZ(gPalette, sizeof(gPalette));
        Mem::FillZ(gVram, sizeof(gVram));
        Mem::FillZ(gOam, sizeof(gOam));

        SetDefaultKeyInput();
        ResetFlashState();
        ResetDma();
        ResetTimers();
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

        // Cerminan ROM pada 0x08000000, 0x0A000000, dan 0x0C000000 (masing-masing 32MB).
        const TUint32 offset = aAddress & 0x01FFFFFFu;

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
            return gBios[aAddress];

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

            if (IsDmaIoAddress(aAddress))
            {
                const TUint32 offset = aAddress - 0x040000B0u;
                const TInt channel = static_cast<TInt>(offset / 12u);
                const TUint32 regOffset = offset % 12u;

                if (regOffset == 0xAu)
                    return static_cast<TUint8>(gDma[channel].controlReg & 0xFFu);
                if (regOffset == 0xBu)
                    return static_cast<TUint8>(gDma[channel].controlReg >> 8);
                return 0;
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
            const TUint32 offset = aAddress - KIoBase;

            // Register IF (0x04000202) bersifat Write-1-to-Clear (W1C).
            if (aAddress == KRegIf || aAddress == KRegIf + 1u)
            {
                gIo[offset] = static_cast<TUint8>(gIo[offset] & ~aValue);
                return;
            }

            // KEYINPUT (0x04000130) bersifat read-only dari sisi CPU.
            if (aAddress == KRegKeyInput || aAddress == KRegKeyInput + 1u)
                return;

            gIo[offset] = aValue;

            if (IsPpuIoAddress(aAddress))
            {
                const TUint32 aligned = aAddress & ~1u;
                const TUint32 ioOffset = aligned - KIoBase;

                const TUint16 value =
                    static_cast<TUint16>(
                        gIo[ioOffset] |
                        (static_cast<TUint16>(
                            gIo[ioOffset + 1]
                        ) << 8)
                    );

                NgbaPpuWriteIo16(aligned, value);
            }
            else if (IsDmaIoAddress(aAddress))
            {
                const TUint32 aligned = aAddress & ~1u;
                const TUint32 ioOffset = aligned - KIoBase;

                const TUint16 value =
                    static_cast<TUint16>(
                        gIo[ioOffset] |
                        (static_cast<TUint16>(
                            gIo[ioOffset + 1]
                        ) << 8)
                    );

                WriteDmaIo16(aligned, value);
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

    NgbaPpuReset();
    BindPpuCallbacks();

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
    BindPpuCallbacks();

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
        ResetDma();
    }

    SetDefaultKeyInput();
    NgbaPpuReset();
    BindPpuCallbacks();
}

TUint8 NgbaMemoryRead8(TUint32 aAddress)
{
    return ReadMemoryByte(aAddress);
}

TUint16 NgbaMemoryRead16(TUint32 aAddress)
{
    aAddress &= ~1u;

    if (aAddress >= KIoBase &&
        aAddress < KIoBase + KIoSize)
    {
        if (IsPpuReadableRegister(aAddress))
            return NgbaPpuReadIo16(aAddress);

        if (aAddress >= 0x04000100u && aAddress < 0x04000110u)
        {
            const TUint32 rel = aAddress - 0x04000100u;
            const TInt idx = static_cast<TInt>(rel / 4u);
            const TUint32 regOff = rel % 4u;
            if (idx >= 0 && idx < 4)
            {
                if (regOff == 0u)
                    return gTimers[idx].counter;
                if (regOff == 2u)
                    return gTimers[idx].control;
            }
        }
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

        // Register IF (0x04000202) bersifat Write-1-to-Clear (W1C).
        if (aAddress == KRegIf)
        {
            const TUint16 currentIf = static_cast<TUint16>(
                gIo[offset] | (static_cast<TUint16>(gIo[offset + 1]) << 8)
            );
            const TUint16 clearedMask = static_cast<TUint16>(currentIf & aValue);
            const TUint16 updatedIf =
                static_cast<TUint16>(currentIf & ~aValue);
            gIo[offset] = static_cast<TUint8>(updatedIf);
            gIo[offset + 1] = static_cast<TUint8>(updatedIf >> 8);
            if (clearedMask != 0u)
            {
                const TUint16 prevWake = NgbaMemoryRead16(0x03007FF8u);
                NgbaMemoryWrite16(0x03007FF8u, static_cast<TUint16>(prevWake | clearedMask));
            }
            return;
        }

        // KEYINPUT (0x04000130) bersifat read-only dari CPU.
        if (aAddress == KRegKeyInput)
            return;

        gIo[offset] = static_cast<TUint8>(aValue);
        gIo[offset + 1] = static_cast<TUint8>(aValue >> 8);

        if (IsPpuIoAddress(aAddress))
            NgbaPpuWriteIo16(aAddress, aValue);
        else if (IsDmaIoAddress(aAddress))
            WriteDmaIo16(aAddress, aValue);
        else if (aAddress >= 0x04000100u && aAddress < 0x04000110u)
            WriteTimerIo16(aAddress, aValue);
        else if (aAddress >= 0x04000120u && aAddress <= 0x0400012Au)
            HandleSerialWrite16(aAddress, aValue);

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

void NgbaMemoryRaiseIrq(TUint16 aIrqFlag)
{
    if (!gReady)
        return;

    const TUint32 offset = KRegIf - KIoBase;
    const TUint16 currentIf = static_cast<TUint16>(
        gIo[offset] | (static_cast<TUint16>(gIo[offset + 1]) << 8)
    );
    const TUint16 updatedIf = static_cast<TUint16>(currentIf | aIrqFlag);

    gIo[offset] = static_cast<TUint8>(updatedIf);
    gIo[offset + 1] = static_cast<TUint8>(updatedIf >> 8);
}

void NgbaMemorySetKeyInput(TUint16 aKeyMask)
{
    const TUint16 masked = aKeyMask & KKeyAllReleased;
    gIo[0x130] = static_cast<TUint8>(masked & 0xFFu);
    gIo[0x131] = static_cast<TUint8>((masked >> 8) & 0xFFu);
}

void NgbaMemoryPressKey(TUint16 aKeyBit)
{
    TUint16 current = static_cast<TUint16>(
        gIo[0x130] | (static_cast<TUint16>(gIo[0x131]) << 8)
    );
    current = static_cast<TUint16>((current & ~aKeyBit) & KKeyAllReleased);
    gIo[0x130] = static_cast<TUint8>(current & 0xFFu);
    gIo[0x131] = static_cast<TUint8>((current >> 8) & 0xFFu);
}

void NgbaMemoryReleaseKey(TUint16 aKeyBit)
{
    TUint16 current = static_cast<TUint16>(
        gIo[0x130] | (static_cast<TUint16>(gIo[0x131]) << 8)
    );
    current = static_cast<TUint16>((current | aKeyBit) & KKeyAllReleased);
    gIo[0x130] = static_cast<TUint8>(current & 0xFFu);
    gIo[0x131] = static_cast<TUint8>((current >> 8) & 0xFFu);
}

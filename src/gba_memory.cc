
#include "gba_memory.h"
#include "game_config.h"
#include "ngba_paths.h"

#include <e32std.h>
#include <f32file.h>

namespace
{
    // -----------------------------------------------------
    // MEMORY SIZE
    // -----------------------------------------------------

    const TInt KEwramSize = 0x40000;
    const TInt KIwramSize = 0x8000;
    const TInt KIoSize = 0x400;
    const TInt KPaletteSize = 0x400;
    const TInt KVramSize = 0x18000;
    const TInt KOamSize = 0x400;

    const TInt KSramSize = 0x10000;
    const TInt KEepromSmallSize = 0x200;
    const TInt KEepromLargeSize = 0x2000;
    const TInt KFlashSmallSize = 0x10000;
    const TInt KFlashLargeSize = 0x20000;

    const TInt KRomCacheSize = 4096;

    // -----------------------------------------------------
    // MEMORY ADDRESS
    // -----------------------------------------------------

    const TUint32 KEwramBase = 0x02000000u;
    const TUint32 KIwramBase = 0x03000000u;
    const TUint32 KIoBase = 0x04000000u;
    const TUint32 KPaletteBase = 0x05000000u;
    const TUint32 KVramBase = 0x06000000u;
    const TUint32 KOamBase = 0x07000000u;
    const TUint32 KRomBase = 0x08000000u;
    const TUint32 KEepromBase = 0x0D000000u;
    const TUint32 KSramBase = 0x0E000000u;

    // -----------------------------------------------------
    // MEMORY ARRAYS
    // -----------------------------------------------------

    TUint8 gEwram[KEwramSize];
    TUint8 gIwram[KIwramSize];
    TUint8 gIo[KIoSize];
    TUint8 gPalette[KPaletteSize];
    TUint8 gVram[KVramSize];
    TUint8 gOam[KOamSize];

    TUint8 gSaveData[KFlashLargeSize];

    const TUint8* gRom = NULL;

    // -----------------------------------------------------
    // ROM FILE CACHE
    // -----------------------------------------------------

    RFs gRomFs;
    RFile gRomFile;

    TBool gRomFileOpen = EFalse;

    TUint8 gRomCache[KRomCacheSize];

    TInt gRomCacheStart = -1;
    TInt gRomCacheLength = 0;

    TInt gRomSize = 0;

    TBool gReady = EFalse;

    // -----------------------------------------------------
    // SAVE CHIP STATE
    // -----------------------------------------------------

    TNgbaSaveType gSaveType = ENgbaSaveAuto;

    TInt gSaveSize = KSramSize;

    TBool gSaveDirty = EFalse;

    TBool gSaveLoaded = EFalse;

    // -----------------------------------------------------
    // FLASH STATE
    // -----------------------------------------------------

    enum TFlashCommandState
    {
        EFlashReady = 0,
        EFlashUnlock1,
        EFlashUnlock2,
        EFlashProgram,
        EFlashEraseUnlock1,
        EFlashEraseUnlock2,
        EFlashEraseCommand,
        EFlashBankSelect
    };

    TFlashCommandState gFlashState = EFlashReady;

    TBool gFlashIdMode = EFalse;

    TInt gFlashBank = 0;

    // -----------------------------------------------------
    // EEPROM STATE
    // -----------------------------------------------------

    enum TEepromState
    {
        EEepromIdle = 0,
        EEepromOpcode,
        EEepromAddress,
        EEepromWriteData,
        EEepromWriteStop,
        EEepromReadDummy,
        EEepromReadData
    };

    TEepromState gEepromState = EEepromIdle;

    TUint32 gEepromAddress = 0;

    TInt gEepromAddressBits = 0;
    TInt gEepromAddressCount = 0;

    TInt gEepromOpcode = 0;

    TInt gEepromDataBits = 0;

    TInt gEepromReadPosition = 0;

    TUint8 gEepromData[8];

    TInt gEepromReadDummy = 0;

    // -----------------------------------------------------
    // SAVE FILE
    // -----------------------------------------------------

    _LIT(KSaveExtension, ".sav");

    // -----------------------------------------------------
    // FILE MANAGEMENT
    // -----------------------------------------------------

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
    }

    void ResetEeprom()
    {
        gEepromState = EEepromIdle;

        gEepromAddress = 0;
        gEepromAddressBits = 0;
        gEepromAddressCount = 0;

        gEepromOpcode = 0;

        gEepromDataBits = 0;
        gEepromReadPosition = 0;

        gEepromReadDummy = 0;

        Mem::FillZ(gEepromData, sizeof(gEepromData));
    }

    void ResetFlash()
    {
        gFlashState = EFlashReady;
        gFlashIdMode = EFalse;
        gFlashBank = 0;
    }

    // -----------------------------------------------------
    // SAVE TYPE
    // -----------------------------------------------------

    void ConfigureSaveType()
    {
        const TNgbaGameConfig& config =
            NgbaGameConfigCurrent();

        gSaveType = config.saveType;

        switch (gSaveType)
        {
            case ENgbaSaveSram:
                gSaveSize = KSramSize;
                break;

            case ENgbaSaveEeprom:
                if (gRomSize > 0x01000000)
                    gSaveSize = KEepromLargeSize;
                else
                    gSaveSize = KEepromSmallSize;
                break;

            case ENgbaSaveFlash64:
                gSaveSize = KFlashSmallSize;
                break;

            case ENgbaSaveFlash128:
                gSaveSize = KFlashLargeSize;
                break;

            case ENgbaSaveAuto:
            default:
                gSaveType = ENgbaSaveSram;
                gSaveSize = KSramSize;
                break;
        }
    }

    // -----------------------------------------------------
    // SAVE FILE PATH
    // -----------------------------------------------------

    TInt BuildSavePath(TFileName& aPath)
    {
        const TNgbaGameConfig& config =
            NgbaGameConfigCurrent();

        if (config.gameCode.Length() != 4)
            return KErrNotReady;

        // Pastikan pengelola direktori sudah diinisialisasi.
        if (!NgbaPathsReady())
        {
            TInt initError = NgbaPathsInit();

            if (initError != KErrNone)
                return initError;
        }

        TFileName saveDirectory;

        NgbaGetSavePath(saveDirectory);

        aPath.Copy(saveDirectory);

        for (TInt i = 0; i < 4; ++i)
        {
            aPath.Append(
                static_cast<TChar>(config.gameCode[i])
            );
        }

        aPath.Append(KSaveExtension);

        return KErrNone;
    }

    // -----------------------------------------------------
    // LOAD SAVE
    // -----------------------------------------------------

    TInt LoadSaveFile()
    {
        TFileName path;

        TInt error = BuildSavePath(path);

        if (error != KErrNone)
            return error;

        Mem::Fill(
            gSaveData,
            gSaveSize,
            0xFF
        );

        RFs fs;

        error = fs.Connect();

        if (error != KErrNone)
            return error;

        RFile file;

        error = file.Open(
            fs,
            path,
            EFileRead | EFileShareReadersOnly
        );

        if (error == KErrNotFound)
        {
            fs.Close();

            gSaveDirty = EFalse;
            gSaveLoaded = ETrue;

            return KErrNone;
        }

        if (error != KErrNone)
        {
            fs.Close();
            return error;
        }

        TInt fileSize = 0;

        error = file.Size(fileSize);

        if (error == KErrNone && fileSize > 0)
        {
            TInt bytesToRead = fileSize;

            if (bytesToRead > gSaveSize)
                bytesToRead = gSaveSize;

            TPtr8 descriptor(
                gSaveData,
                0,
                gSaveSize
            );

            error = file.Read(
                0,
                descriptor,
                bytesToRead
            );

            if (error == KErrNone &&
                descriptor.Length() != bytesToRead)
            {
                error = KErrEof;
            }
        }

        file.Close();
        fs.Close();

        if (error == KErrNone)
        {
            gSaveDirty = EFalse;
            gSaveLoaded = ETrue;
        }

        return error;
    }

    // -----------------------------------------------------
    // SAVE FLUSH
    // -----------------------------------------------------

    TInt FlushSaveFile()
    {
        if (!gSaveLoaded || !gSaveDirty)
            return KErrNone;

        TFileName path;

        TInt error = BuildSavePath(path);

        if (error != KErrNone)
            return error;

        RFs fs;

        error = fs.Connect();

        if (error != KErrNone)
            return error;

        RFile file;

        error = file.Replace(
            fs,
            path,
            EFileWrite | EFileShareExclusive
        );

        if (error != KErrNone)
        {
            fs.Close();
            return error;
        }

        TPtrC8 descriptor(
            gSaveData,
            gSaveSize
        );

        error = file.Write(descriptor);

        file.Close();
        fs.Close();

        if (error == KErrNone)
            gSaveDirty = EFalse;

        return error;
    }

    // -----------------------------------------------------
    // MEMORY ARRAY
    // -----------------------------------------------------

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

    // -----------------------------------------------------
    // ROM CACHE
    // -----------------------------------------------------

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

    // -----------------------------------------------------
    // SRAM
    // -----------------------------------------------------

    TUint8 ReadSram(TUint32 aAddress)
    {
        const TUint32 offset =
            aAddress - KSramBase;

        if (gSaveType != ENgbaSaveSram)
            return 0xFF;

        return gSaveData[
            offset % static_cast<TUint32>(gSaveSize)
        ];
    }

    void WriteSram(
        TUint32 aAddress,
        TUint8 aValue
    )
    {
        if (gSaveType != ENgbaSaveSram)
            return;

        const TUint32 offset =
            aAddress - KSramBase;

        const TInt index = static_cast<TInt>(
            offset % static_cast<TUint32>(gSaveSize)
        );

        if (gSaveData[index] != aValue)
        {
            gSaveData[index] = aValue;
            gSaveDirty = ETrue;
        }
    }

    // -----------------------------------------------------
    // EEPROM
    // -----------------------------------------------------

    TInt EepromAddressBits()
    {
        if (gSaveSize == KEepromLargeSize)
            return 14;

        return 6;
    }

    TUint32 EepromBlockAddress()
    {
        if (gSaveSize == KEepromLargeSize)
            return gEepromAddress & 0x03FFu;

        return gEepromAddress & 0x003Fu;
    }

    void EepromBeginCommand()
    {
        gEepromState = EEepromOpcode;

        gEepromOpcode = 0;
        gEepromAddress = 0;
        gEepromAddressCount = 0;
        gEepromAddressBits = EepromAddressBits();

        gEepromDataBits = 0;
        gEepromReadPosition = 0;
    }

    void EepromWriteBit(TUint16 aValue)
    {
        const TUint8 bit =
            static_cast<TUint8>(aValue & 1u);

        switch (gEepromState)
        {
            case EEepromIdle:
                if (bit)
                    EepromBeginCommand();
                break;

            case EEepromOpcode:
                gEepromOpcode =
                    (gEepromOpcode << 1) | bit;

                if (++gEepromAddressCount >= 2)
                {
                    gEepromAddressCount = 0;

                    if (gEepromOpcode == 2 ||
                        gEepromOpcode == 3)
                    {
                        gEepromState = EEepromAddress;
                    }
                    else
                    {
                        ResetEeprom();
                    }
                }
                break;

            case EEepromAddress:
                gEepromAddress =
                    (gEepromAddress << 1) | bit;

                ++gEepromAddressCount;

                if (gEepromAddressCount >=
                    gEepromAddressBits)
                {
                    gEepromAddressCount = 0;

                    if (gEepromOpcode == 2)
                    {
                        Mem::FillZ(
                            gEepromData,
                            sizeof(gEepromData)
                        );

                        gEepromDataBits = 0;
                        gEepromState = EEepromWriteData;
                    }
                    else
                    {
                        const TUint32 block =
                            EepromBlockAddress();

                        const TUint32 offset =
                            block * 8u;

                        for (TInt i = 0; i < 8; ++i)
                        {
                            gEepromData[i] =
                                gSaveData[
                                    (offset + i) % gSaveSize
                                ];
                        }

                        gEepromReadDummy = 4;
                        gEepromReadPosition = 0;

                        gEepromState = EEepromReadDummy;
                    }
                }
                break;

            case EEepromWriteData:
                {
                    const TInt byteIndex =
                        gEepromDataBits / 8;

                    const TInt bitIndex =
                        7 - (gEepromDataBits % 8);

                    if (bit)
                    {
                        gEepromData[byteIndex] |=
                            static_cast<TUint8>(1u << bitIndex);
                    }

                    ++gEepromDataBits;

                    if (gEepromDataBits >= 64)
                        gEepromState = EEepromWriteStop;
                }
                break;

            case EEepromWriteStop:
                {
                    const TUint32 block =
                        EepromBlockAddress();

                    const TUint32 offset =
                        block * 8u;

                    for (TInt i = 0; i < 8; ++i)
                    {
                        const TInt index =
                            static_cast<TInt>(
                                (offset + i) % gSaveSize
                            );

                        if (gSaveData[index] != gEepromData[i])
                        {
                            gSaveData[index] = gEepromData[i];
                            gSaveDirty = ETrue;
                        }
                    }

                    ResetEeprom();
                }
                break;

            default:
                ResetEeprom();
                break;
        }
    }

    TUint16 EepromReadBit()
    {
        if (gEepromState == EEepromReadDummy)
        {
            --gEepromReadDummy;

            if (gEepromReadDummy <= 0)
                gEepromState = EEepromReadData;

            return 1;
        }

        if (gEepromState == EEepromReadData)
        {
            const TInt byteIndex =
                gEepromReadPosition / 8;

            const TInt bitIndex =
                7 - (gEepromReadPosition % 8);

            const TUint16 result =
                (gEepromData[byteIndex] >> bitIndex) & 1u;

            ++gEepromReadPosition;

            if (gEepromReadPosition >= 64)
                ResetEeprom();

            return result;
        }

        return 1;
    }

    // -----------------------------------------------------
    // FLASH
    // -----------------------------------------------------

    TInt FlashPhysicalOffset(TUint32 aAddress)
    {
        TInt offset = static_cast<TInt>(
            aAddress & 0xFFFFu
        );

        if (gSaveSize == KFlashLargeSize)
            offset += gFlashBank * 0x10000;

        if (offset < 0 || offset >= gSaveSize)
            return 0;

        return offset;
    }

    TUint8 ReadFlash(TUint32 aAddress)
    {
        if (gFlashIdMode)
        {
            const TInt address =
                static_cast<TInt>(aAddress & 0xFFFFu);

            if (address == 0)
                return 0xC2;

            if (address == 1)
            {
                if (gSaveSize == KFlashLargeSize)
                    return 0x09;

                return 0x1C;
            }

            return 0xFF;
        }

        return gSaveData[
            FlashPhysicalOffset(aAddress)
        ];
    }

    void FlashEraseSector(TUint32 aAddress)
    {
        const TInt offset =
            FlashPhysicalOffset(aAddress);

        const TInt sectorStart =
            (offset / 0x1000) * 0x1000;

        TInt end = sectorStart + 0x1000;

        if (end > gSaveSize)
            end = gSaveSize;

        for (TInt i = sectorStart; i < end; ++i)
            gSaveData[i] = 0xFF;

        gSaveDirty = ETrue;
    }

    void FlashWrite(
        TUint32 aAddress,
        TUint8 aValue
    )
    {
        const TUint32 address =
            aAddress & 0xFFFFu;

        switch (gFlashState)
        {
            case EFlashReady:
                if (address == 0x5555u &&
                    aValue == 0xAA)
                {
                    gFlashState = EFlashUnlock1;
                }
                else if (aValue == 0xF0)
                {
                    gFlashIdMode = EFalse;
                    gFlashState = EFlashReady;
                }
                break;

            case EFlashUnlock1:
                if (address == 0x2AAAu &&
                    aValue == 0x55)
                {
                    gFlashState = EFlashUnlock2;
                }
                else
                {
                    gFlashState = EFlashReady;
                }
                break;

            case EFlashUnlock2:
                if (address == 0x5555u)
                {
                    if (aValue == 0x90)
                    {
                        gFlashIdMode = ETrue;
                        gFlashState = EFlashReady;
                    }
                    else if (aValue == 0xA0)
                    {
                        gFlashState = EFlashProgram;
                    }
                    else if (aValue == 0x80)
                    {
                        gFlashState = EFlashEraseUnlock1;
                    }
                    else if (aValue == 0xB0 &&
                        gSaveSize == KFlashLargeSize)
                    {
                        gFlashState = EFlashBankSelect;
                    }
                    else
                    {
                        gFlashState = EFlashReady;
                    }
                }
                else
                {
                    gFlashState = EFlashReady;
                }
                break;

            case EFlashProgram:
                {
                    const TInt offset =
                        FlashPhysicalOffset(aAddress);

                    const TUint8 value =
                        gSaveData[offset] & aValue;

                    if (gSaveData[offset] != value)
                    {
                        gSaveData[offset] = value;
                        gSaveDirty = ETrue;
                    }

                    gFlashState = EFlashReady;
                }
                break;

            case EFlashEraseUnlock1:
                if (address == 0x5555u &&
                    aValue == 0xAA)
                {
                    gFlashState = EFlashEraseUnlock2;
                }
                else
                {
                    gFlashState = EFlashReady;
                }
                break;

            case EFlashEraseUnlock2:
                if (address == 0x2AAAu &&
                    aValue == 0x55)
                {
                    gFlashState = EFlashEraseCommand;
                }
                else
                {
                    gFlashState = EFlashReady;
                }
                break;

            case EFlashEraseCommand:
                if (address == 0x5555u &&
                    aValue == 0x10)
                {
                    Mem::Fill(
                        gSaveData,
                        gSaveSize,
                        0xFF
                    );

                    gSaveDirty = ETrue;
                }
                else if (aValue == 0x30)
                {
                    FlashEraseSector(aAddress);
                }

                gFlashState = EFlashReady;
                break;

            case EFlashBankSelect:
                gFlashBank = aValue & 1;
                gFlashState = EFlashReady;
                break;

            default:
                gFlashState = EFlashReady;
                break;
        }

        if (aValue == 0xF0)
        {
            gFlashIdMode = EFalse;
            gFlashState = EFlashReady;
        }
    }

    // -----------------------------------------------------
    // MEMORY READ
    // -----------------------------------------------------

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
            aAddress < KEepromBase)
        {
            return ReadRom(aAddress);
        }

        if (aAddress >= KSramBase &&
            aAddress < KSramBase + 0x01000000u)
        {
            if (gSaveType == ENgbaSaveFlash64 ||
                gSaveType == ENgbaSaveFlash128)
            {
                return ReadFlash(aAddress);
            }

            return ReadSram(aAddress);
        }

        return 0;
    }

    // -----------------------------------------------------
    // MEMORY WRITE
    // -----------------------------------------------------

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
            gIo[aAddress - KIoBase] = aValue;
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
            aAddress < KSramBase)
            return;

        if (aAddress >= KSramBase &&
            aAddress < KSramBase + 0x01000000u)
        {
            if (gSaveType == ENgbaSaveFlash64 ||
                gSaveType == ENgbaSaveFlash128)
            {
                FlashWrite(aAddress, aValue);
            }
            else
            {
                WriteSram(aAddress, aValue);
            }
        }
    }
}

// =========================================================
// PUBLIC API
// =========================================================

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

    ConfigureSaveType();

    ResetEeprom();
    ResetFlash();

    TInt error = LoadSaveFile();

    if (error != KErrNone)
        return error;

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

    ConfigureSaveType();

    ResetEeprom();
    ResetFlash();

    error = LoadSaveFile();

    if (error != KErrNone)
    {
        NgbaMemoryShutdown();
        return error;
    }

    gReady = ETrue;

    return KErrNone;
}

void NgbaMemoryShutdown()
{
    FlushSaveFile();

    gReady = EFalse;

    CloseRomFile();

    gRom = NULL;
    gRomSize = 0;

    gRomCacheStart = -1;
    gRomCacheLength = 0;

    gSaveDirty = EFalse;
    gSaveLoaded = EFalse;

    gSaveType = ENgbaSaveAuto;
    gSaveSize = KSramSize;

    ResetEeprom();
    ResetFlash();

    ClearMemory();
}

TInt NgbaMemorySaveFlush()
{
    return FlushSaveFile();
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

// ---------------------------------------------------------
// READ 8
// ---------------------------------------------------------

TUint8 NgbaMemoryRead8(TUint32 aAddress)
{
    return ReadMemoryByte(aAddress);
}

// ---------------------------------------------------------
// READ 16
// ---------------------------------------------------------

TUint16 NgbaMemoryRead16(TUint32 aAddress)
{
    aAddress &= ~1u;

    if (aAddress >= KEepromBase &&
        aAddress < KSramBase &&
        gSaveType == ENgbaSaveEeprom)
    {
        return EepromReadBit();
    }

    const TUint16 b0 =
        ReadMemoryByte(aAddress);

    const TUint16 b1 =
        ReadMemoryByte(aAddress + 1);

    return static_cast<TUint16>(
        b0 | (b1 << 8)
    );
}

// ---------------------------------------------------------
// READ 32
// ---------------------------------------------------------

TUint32 NgbaMemoryRead32(TUint32 aAddress)
{
    aAddress &= ~3u;

    if (aAddress >= KEepromBase &&
        aAddress < KSramBase &&
        gSaveType == ENgbaSaveEeprom)
    {
        const TUint32 b0 = EepromReadBit();
        const TUint32 b1 = EepromReadBit();

        return b0 | (b1 << 16);
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

// ---------------------------------------------------------
// WRITE 8
// ---------------------------------------------------------

void NgbaMemoryWrite8(
    TUint32 aAddress,
    TUint8 aValue
)
{
    WriteMemoryByte(aAddress, aValue);
}

// ---------------------------------------------------------
// WRITE 16
// ---------------------------------------------------------

void NgbaMemoryWrite16(
    TUint32 aAddress,
    TUint16 aValue
)
{
    aAddress &= ~1u;

    if (aAddress >= KEepromBase &&
        aAddress < KSramBase &&
        gSaveType == ENgbaSaveEeprom)
    {
        EepromWriteBit(aValue);
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

// ---------------------------------------------------------
// WRITE 32
// ---------------------------------------------------------

void NgbaMemoryWrite32(
    TUint32 aAddress,
    TUint32 aValue
)
{
    aAddress &= ~3u;

    if (aAddress >= KEepromBase &&
        aAddress < KSramBase &&
        gSaveType == ENgbaSaveEeprom)
    {
        EepromWriteBit(
            static_cast<TUint16>(aValue)
        );

        EepromWriteBit(
            static_cast<TUint16>(aValue >> 16)
        );

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

// ---------------------------------------------------------
// MEMORY STATUS
// ---------------------------------------------------------

TBool NgbaMemoryReady()
{
    return gReady;
}

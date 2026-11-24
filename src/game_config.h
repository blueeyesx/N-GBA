
#ifndef NGBA_GAME_CONFIG_H
#define NGBA_GAME_CONFIG_H

#include <e32std.h>

enum TNgbaSaveType
{
    ENgbaSaveAuto = 0,
    ENgbaSaveSram,
    ENgbaSaveEeprom,
    ENgbaSaveFlash64,
    ENgbaSaveFlash128
};

struct TNgbaGameConfig
{
    TBuf8<4> gameCode;

    TNgbaSaveType saveType;

    TBool rtcEnabled;
    TBool cpuSwiHle;
    TBool memoryFix;
    TBool timingFix;
    TBool dmaFix;
    TBool irqFix;
    TBool ppuFix;
    TBool audioFix;

    TInt frameskip;
};

// Mengembalikan konfigurasi standar.
void NgbaGameConfigReset();

// Memuat konfigurasi berdasarkan kode game.
TInt NgbaGameConfigLoad(const TDesC8& aGameCode);

// Mengambil konfigurasi aktif.
const TNgbaGameConfig& NgbaGameConfigCurrent();

#endif

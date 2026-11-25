#include "ngba_settings_fps_helper.h"

TBool NgbaToggleShowFps()
{
    const TBool nextState = !NgbaPpuGetShowFps();
    NgbaPpuSetShowFps(nextState);
    return nextState;
}

void NgbaGetFpsMenuLabel(TDes& aDest, TBool aIndonesian)
{
    aDest.Zero();
    const TBool enabled = NgbaPpuGetShowFps();

    if (aIndonesian)
    {
        _LIT(KPrefixId, "FPS: ");
        _LIT(KOnId, "AKTIF");
        _LIT(KOffId, "NONAKTIF");
        aDest.Append(KPrefixId);
        aDest.Append(enabled ? KOnId : KOffId);
    }
    else
    {
        _LIT(KPrefixEn, "FPS: ");
        _LIT(KOnEn, "ON");
        _LIT(KOffEn, "OFF");
        aDest.Append(KPrefixEn);
        aDest.Append(enabled ? KOnEn : KOffEn);
    }
}

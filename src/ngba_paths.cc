
#include "ngba_paths.h"

#include <e32std.h>
#include <f32file.h>

namespace
{
    TBool gPathsReady = EFalse;
    TChar gDrive = 'E';

    TFileName gRootPath;

    _LIT(KDriveE, "E:\\GBA\\");
    _LIT(KDriveC, "C:\\GBA\\");

    _LIT(KRomFolder, "ROM\\");
    _LIT(KSaveFolder, "Saves\\");
    _LIT(KSaveStateFolder, "SaveStates\\");
    _LIT(KConfigFolder, "Config\\");
    _LIT(KLogFolder, "Logs\\");
    _LIT(KSettingsFolder, "Settings\\");
    _LIT(KCoversFolder, "Covers\\");
    _LIT(KWallpapersFolder, "Wallpapers\\");
    _LIT(KCheatsFolder, "Cheats\\");

    TInt CreateFolder(RFs& aFs, const TDesC& aPath)
    {
        TInt error = aFs.MkDirAll(aPath);

        if (error == KErrNone ||
            error == KErrAlreadyExists)
        {
            return KErrNone;
        }

        return error;
    }

    TInt CreateAllFolders(RFs& aFs)
    {
        TFileName path;

        path.Copy(gRootPath);

        TInt error = CreateFolder(aFs, path);

        if (error != KErrNone)
            return error;

        path.Copy(gRootPath);
        path.Append(KRomFolder);

        error = CreateFolder(aFs, path);

        if (error != KErrNone)
            return error;

        path.Copy(gRootPath);
        path.Append(KSaveFolder);

        error = CreateFolder(aFs, path);

        if (error != KErrNone)
            return error;

        path.Copy(gRootPath);
        path.Append(KSaveStateFolder);

        error = CreateFolder(aFs, path);

        if (error != KErrNone)
            return error;

        path.Copy(gRootPath);
        path.Append(KConfigFolder);

        error = CreateFolder(aFs, path);

        if (error != KErrNone)
            return error;

        path.Copy(gRootPath);
        path.Append(KLogFolder);

        error = CreateFolder(aFs, path);

        if (error != KErrNone)
            return error;

        path.Copy(gRootPath);
        path.Append(KSettingsFolder);

        error = CreateFolder(aFs, path);

        if (error != KErrNone)
            return error;

        path.Copy(gRootPath);
        path.Append(KCoversFolder);

        error = CreateFolder(aFs, path);

        if (error != KErrNone)
            return error;

        path.Copy(gRootPath);
        path.Append(KWallpapersFolder);

        error = CreateFolder(aFs, path);

        if (error != KErrNone)
            return error;

        path.Copy(gRootPath);
        path.Append(KCheatsFolder);

        return CreateFolder(aFs, path);
    }
}

// ---------------------------------------------------------
// INITIALIZE
// ---------------------------------------------------------

TInt NgbaPathsInit()
{
    gPathsReady = EFalse;

    RFs fs;

    TInt error = fs.Connect();

    if (error != KErrNone)
        return error;

    // Prioritas pertama E:\GBA\
    gRootPath.Copy(KDriveE);

    error = CreateAllFolders(fs);

    if (error == KErrNone)
    {
        gDrive = 'E';
        gPathsReady = ETrue;

        fs.Close();
        return KErrNone;
    }

    // Jika E: gagal, gunakan C:\GBA\
    gRootPath.Copy(KDriveC);

    error = CreateAllFolders(fs);

    if (error == KErrNone)
    {
        gDrive = 'C';
        gPathsReady = ETrue;
    }

    fs.Close();

    return error;
}

// ---------------------------------------------------------
// DRIVE
// ---------------------------------------------------------

TChar NgbaPathsDrive()
{
    return gDrive;
}

// ---------------------------------------------------------
// ROOT
// ---------------------------------------------------------

void NgbaGetRootPath(TDes& aPath)
{
    aPath.Copy(gRootPath);
}

// ---------------------------------------------------------
// ROM
// ---------------------------------------------------------

void NgbaGetRomPath(TDes& aPath)
{
    aPath.Copy(gRootPath);
    aPath.Append(KRomFolder);
}

// ---------------------------------------------------------
// SAVE
// ---------------------------------------------------------

void NgbaGetSavePath(TDes& aPath)
{
    aPath.Copy(gRootPath);
    aPath.Append(KSaveFolder);
}

// ---------------------------------------------------------
// SAVE STATE
// ---------------------------------------------------------

void NgbaGetSaveStatePath(TDes& aPath)
{
    aPath.Copy(gRootPath);
    aPath.Append(KSaveStateFolder);
}

// ---------------------------------------------------------
// CONFIG
// ---------------------------------------------------------

void NgbaGetConfigPath(TDes& aPath)
{
    aPath.Copy(gRootPath);
    aPath.Append(KConfigFolder);
}

// ---------------------------------------------------------
// LOG
// ---------------------------------------------------------

void NgbaGetLogPath(TDes& aPath)
{
    aPath.Copy(gRootPath);
    aPath.Append(KLogFolder);
}

// ---------------------------------------------------------
// SETTINGS
// ---------------------------------------------------------

void NgbaGetSettingsPath(TDes& aPath)
{
    aPath.Copy(gRootPath);
    aPath.Append(KSettingsFolder);
}

// ---------------------------------------------------------
// COVERS
// ---------------------------------------------------------

void NgbaGetCoversPath(TDes& aPath)
{
    aPath.Copy(gRootPath);
    aPath.Append(KCoversFolder);
}

// ---------------------------------------------------------
// WALLPAPERS
// ---------------------------------------------------------

void NgbaGetWallpapersPath(TDes& aPath)
{
    aPath.Copy(gRootPath);
    aPath.Append(KWallpapersFolder);
}

// ---------------------------------------------------------
// CHEATS
// ---------------------------------------------------------

void NgbaGetCheatsPath(TDes& aPath)
{
    aPath.Copy(gRootPath);
    aPath.Append(KCheatsFolder);
}

// ---------------------------------------------------------
// STATUS
// ---------------------------------------------------------

TBool NgbaPathsReady()
{
    return gPathsReady;
}

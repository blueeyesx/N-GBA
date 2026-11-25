
#include "ngba_paths.h"

#include <e32base.h>
#include <e32std.h>
#include <f32file.h>

namespace
{

_LIT(KOldRoot, "C:\\Data\\N-GBA\\");

_LIT(KEroot, "E:\\GBA\\");
_LIT(KCroot, "C:\\GBA\\");

_LIT(KBios, "BIOS\\");
_LIT(KConfig, "Config\\");
_LIT(KSaves, "Saves\\");
_LIT(KSaveStates, "SaveStates\\");
_LIT(KLogs, "Logs\\");
_LIT(KLanguages, "Languages\\");
_LIT(KFonts, "Fonts\\");
_LIT(KCovers, "Covers\\");
_LIT(KWallpapers, "Wallpapers\\");
_LIT(KCheats, "Cheats\\");
_LIT(KData, "Data\\");

_LIT(KLanguageFile, "language.dat");
_LIT(KFontFile, "font.dat");
_LIT(KLogFile, "ngba.log");
_LIT(KBiosFile, "gba_bios.bin");

const TInt KMaxMigrationDepth = 16;
const TInt KCopyBufferSize = 4096;

TFileName gRootPath;
TChar gDrive = 'C';
TBool gReady = EFalse;

TBool IsAlreadyExists(TInt aError)
{
    return (
        aError == KErrAlreadyExists ||
        aError == KErrNone
    );
}

TInt CreateFolder(RFs& aFs, const TDesC& aPath)
{
    TInt error = aFs.MkDirAll(aPath);

    if (IsAlreadyExists(error))
        return KErrNone;

    return error;
}

TInt AppendPath(
    TFileName& aPath,
    const TDesC& aPart
)
{
    if (aPath.Length() + aPart.Length() >
        aPath.MaxLength())
    {
        return KErrOverflow;
    }

    aPath.Append(aPart);
    return KErrNone;
}

TInt CreateRequiredFolders(
    RFs& aFs,
    const TDesC& aRoot
)
{
    TFileName path;
    path.Copy(aRoot);

    const TDesC* folders[] =
    {
        &KBios,
        &KConfig,
        &KSaves,
        &KSaveStates,
        &KLogs,
        &KLanguages,
        &KFonts,
        &KCovers,
        &KWallpapers,
        &KCheats,
        &KData
    };

    const TInt count =
        sizeof(folders) / sizeof(folders[0]);

    for (TInt i = 0; i < count; ++i)
    {
        path.Copy(aRoot);

        TInt error = AppendPath(
            path,
            *folders[i]
        );

        if (error != KErrNone)
            return error;

        error = CreateFolder(aFs, path);

        if (error != KErrNone)
            return error;
    }

    return KErrNone;
}

TBool IsGbaFile(const TDesC& aName)
{
    if (aName.Length() < 4)
        return EFalse;

    return (
        aName.Right(4).CompareF(_L(".gba")) == 0
    );
}

// Menyalin file menggunakan RFile.
// File tujuan yang sudah ada tidak akan ditimpa.
TInt CopyFileIfMissing(
    RFs& aFs,
    const TDesC& aSource,
    const TDesC& aDestination
)
{
    TEntry entry;

    TInt error = aFs.Entry(
        aDestination,
        entry
    );

    if (error == KErrNone)
        return KErrNone;

    if (error != KErrNotFound &&
        error != KErrPathNotFound)
    {
        return error;
    }

    TParsePtrC destinationParse(aDestination);

    TFileName parent;
    parent.Copy(destinationParse.DriveAndPath());

    error = CreateFolder(aFs, parent);

    if (error != KErrNone)
        return error;

    RFile sourceFile;

    error = sourceFile.Open(
        aFs,
        aSource,
        EFileRead | EFileShareReadersOnly
    );

    if (error != KErrNone)
        return error;

    TInt fileSize = 0;

    error = sourceFile.Size(fileSize);

    if (error != KErrNone)
    {
        sourceFile.Close();
        return error;
    }

    RFile destinationFile;

    error = destinationFile.Create(
        aFs,
        aDestination,
        EFileWrite | EFileShareExclusive
    );

    if (error == KErrAlreadyExists)
    {
        sourceFile.Close();
        return KErrNone;
    }

    if (error != KErrNone)
    {
        sourceFile.Close();
        return error;
    }

    TInt position = 0;

    TBuf8<KCopyBufferSize> buffer;

    while (position < fileSize)
    {
        buffer.Zero();

        TInt remaining = fileSize - position;

        TInt amount = (
            remaining > KCopyBufferSize
            ? KCopyBufferSize
            : remaining
        );

        error = sourceFile.Read(
            position,
            buffer,
            amount
        );

        if (error != KErrNone)
            break;

        if (buffer.Length() == 0)
        {
            error = KErrCorrupt;
            break;
        }

        error = destinationFile.Write(
            position,
            buffer
        );

        if (error != KErrNone)
            break;

        position += buffer.Length();
    }

    destinationFile.Close();
    sourceFile.Close();

    // Hapus file parsial jika penyalinan gagal.
    if (error != KErrNone)
    {
        aFs.Delete(aDestination);
        return error;
    }

    return KErrNone;
}

TInt MigrateFolder(
    RFs& aFs,
    const TDesC& aSourceFolder,
    const TDesC& aTargetRoot,
    TInt aDepth
)
{
    if (aDepth > KMaxMigrationDepth)
        return KErrTooBig;

    CDir* files = NULL;
    CDir* directories = NULL;

    TInt error = aFs.GetDir(
        aSourceFolder,
        KEntryAttNormal,
        ESortByName,
        files,
        directories
    );

    if (error != KErrNone)
    {
        delete files;
        delete directories;
        return error;
    }

    // Salin file.
    if (files != NULL)
    {
        for (
            TInt i = 0;
            i < files->Count();
            ++i
        )
        {
            const TEntry& entry = (*files)[i];

            TFileName source;
            source.Copy(aSourceFolder);

            if (AppendPath(source, entry.iName) != KErrNone)
                continue;

            TFileName destination;

            // Semua ROM GBA berada langsung di root.
            if (IsGbaFile(entry.iName))
            {
                destination.Copy(aTargetRoot);
            }
            else
            {
                destination.Copy(aTargetRoot);

                TPtrC relative(
                    aSourceFolder.Mid(
                        KOldRoot().Length()
                    )
                );

                if (AppendPath(destination, relative) != KErrNone)
                    continue;
            }

            if (AppendPath(destination, entry.iName) != KErrNone)
                continue;

            // Kegagalan satu file tidak menghentikan migrasi.
            CopyFileIfMissing(
                aFs,
                source,
                destination
            );
        }
    }

    // Salin subfolder secara rekursif.
    if (directories != NULL)
    {
        for (
            TInt i = 0;
            i < directories->Count();
            ++i
        )
        {
            const TEntry& entry = (*directories)[i];

            TFileName sourceFolder;
            sourceFolder.Copy(aSourceFolder);

            if (AppendPath(
                sourceFolder,
                entry.iName
            ) != KErrNone)
            {
                continue;
            }

            if (AppendPath(
                sourceFolder,
                _L("\\")
            ) != KErrNone)
            {
                continue;
            }

            TFileName destinationFolder;
            destinationFolder.Copy(aTargetRoot);

            TPtrC relative(
                sourceFolder.Mid(
                    KOldRoot().Length()
                )
            );

            if (AppendPath(
                destinationFolder,
                relative
            ) != KErrNone)
            {
                continue;
            }

            CreateFolder(
                aFs,
                destinationFolder
            );

            MigrateFolder(
                aFs,
                sourceFolder,
                aTargetRoot,
                aDepth + 1
            );
        }
    }

    delete files;
    delete directories;

    return KErrNone;
}

TInt SelectStorage(RFs& aFs)
{
    TInt error = CreateFolder(
        aFs,
        KEroot
    );

    if (error == KErrNone)
    {
        error = CreateRequiredFolders(
            aFs,
            KEroot
        );

        if (error == KErrNone)
        {
            gRootPath.Copy(KEroot);
            gDrive = 'E';
            return KErrNone;
        }
    }

    // Fallback ke memori telepon.
    error = CreateFolder(
        aFs,
        KCroot
    );

    if (error != KErrNone)
        return error;

    error = CreateRequiredFolders(
        aFs,
        KCroot
    );

    if (error != KErrNone)
        return error;

    gRootPath.Copy(KCroot);
    gDrive = 'C';

    return KErrNone;
}

void BuildSubPath(
    TDes& aPath,
    const TDesC& aFolder
)
{
    aPath.Copy(gRootPath);
    aPath.Append(aFolder);
}

void BuildFilePath(
    TDes& aPath,
    const TDesC& aFolder,
    const TDesC& aFile
)
{
    BuildSubPath(aPath, aFolder);
    aPath.Append(aFile);
}

}

// --------------------------------------------------
// PUBLIC API
// --------------------------------------------------

TInt NgbaPathsInit()
{
    gReady = EFalse;
    gRootPath.Zero();

    RFs fs;

    TInt error = fs.Connect();

    if (error != KErrNone)
        return error;

    error = SelectStorage(fs);

    if (error == KErrNone)
    {
        // Migrasi data lama.
        // Sumber lama tidak dihapus.
        // Kegagalan menyalin satu file tidak
        // membatalkan inisialisasi direktori.
        MigrateFolder(
            fs,
            KOldRoot,
            gRootPath,
            0
        );

        gReady = ETrue;
    }

    fs.Close();

    return error;
}

TBool NgbaPathsReady()
{
    return gReady;
}

TChar NgbaPathsDrive()
{
    return gDrive;
}

void NgbaGetRootPath(TDes& aPath)
{
    aPath.Copy(gRootPath);
}

void NgbaGetBiosPath(TDes& aPath)
{
    BuildSubPath(aPath, KBios);
}

void NgbaGetConfigPath(TDes& aPath)
{
    BuildSubPath(aPath, KConfig);
}

void NgbaGetSavePath(TDes& aPath)
{
    BuildSubPath(aPath, KSaves);
}

void NgbaGetSaveStatePath(TDes& aPath)
{
    BuildSubPath(aPath, KSaveStates);
}

void NgbaGetLogPath(TDes& aPath)
{
    BuildSubPath(aPath, KLogs);
}

void NgbaGetLanguagePath(TDes& aPath)
{
    BuildSubPath(aPath, KLanguages);
}

void NgbaGetFontPath(TDes& aPath)
{
    BuildSubPath(aPath, KFonts);
}

void NgbaGetCoversPath(TDes& aPath)
{
    BuildSubPath(aPath, KCovers);
}

void NgbaGetWallpapersPath(TDes& aPath)
{
    BuildSubPath(aPath, KWallpapers);
}

void NgbaGetCheatsPath(TDes& aPath)
{
    BuildSubPath(aPath, KCheats);
}

void NgbaGetDataPath(TDes& aPath)
{
    BuildSubPath(aPath, KData);
}

void NgbaGetLanguageFilePath(TDes& aPath)
{
    BuildFilePath(
        aPath,
        KLanguages,
        KLanguageFile
    );
}

void NgbaGetFontFilePath(TDes& aPath)
{
    BuildFilePath(
        aPath,
        KFonts,
        KFontFile
    );
}

void NgbaGetLogFilePath(TDes& aPath)
{
    BuildFilePath(
        aPath,
        KLogs,
        KLogFile
    );
}

void NgbaGetBiosFilePath(TDes& aPath)
{
    BuildFilePath(
        aPath,
        KBios,
        KBiosFile
    );
}

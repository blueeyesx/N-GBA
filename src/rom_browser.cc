
#include "rom_browser.h"

#include <f32file.h>

namespace {

TBool IsGbaFile(const TDesC& aName)
{
    if (aName.Length() < 4) {
        return EFalse;
    }

    return aName.Right(4).CompareF(_L(".gba")) == 0;
}

void ScanFolder(
    RFs& aFs,
    const TDesC& aFolder,
    TFileName* aFiles,
    TInt aMaxFiles,
    TInt& aCount
)
{
    if (aCount >= aMaxFiles) {
        return;
    }

    TFileName pattern;
    pattern.Copy(aFolder);
    pattern.Append(_L("*.gba"));

    CDir* entries = NULL;

    TInt error = aFs.GetDir(
        pattern,
        KEntryAttNormal,
        ESortByName,
        entries
    );

    if (error != KErrNone || entries == NULL) {
        delete entries;
        return;
    }

    for (TInt i = 0;
         i < entries->Count() && aCount < aMaxFiles;
         ++i) {

        const TEntry& entry = (*entries)[i];

        if (!IsGbaFile(entry.iName)) {
            continue;
        }

        TFileName fullPath;
        fullPath.Copy(aFolder);
        fullPath.Append(entry.iName);

        aFiles[aCount] = fullPath;
        ++aCount;
    }

    delete entries;
}

}

TInt ScanRoms(TFileName* aFiles, TInt aMaxFiles)
{
    if (aFiles == NULL || aMaxFiles <= 0) {
        return KErrArgument;
    }

    TInt count = 0;

    RFs fs;
    TInt error = fs.Connect();

    if (error != KErrNone) {
        return error;
    }

    // Phone memory
    ScanFolder(
        fs,
        _L("C:\\GBA\\"),
        aFiles,
        aMaxFiles,
        count
    );

    // Memory card
    ScanFolder(
        fs,
        _L("E:\\GBA\\"),
        aFiles,
        aMaxFiles,
        count
    );

    fs.Close();

    return count;
}

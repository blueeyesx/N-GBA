// -----------------------------------------------------
// CREATE SAVE DIRECTORY
// -----------------------------------------------------

TInt EnsureSaveDirectory(RFs& aFs)
{
    TInt error = aFs.MkDirAll(KSaveDirectory);

    if (error == KErrNone ||
        error == KErrAlreadyExists)
    {
        return KErrNone;
    }

    return error;
}

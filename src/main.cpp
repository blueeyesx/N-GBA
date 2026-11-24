
#include <e32base.h>
#include <e32std.h>
#include <f32file.h>

#include "window_server.h"

namespace
{

void WriteStartupLog(const TDesC8& aMessage)
{
    RFs fs;

    if (fs.Connect() != KErrNone)
        return;

    RFile file;

    TInt error = file.Open(
        fs,
        _L("C:\\ngba_startup.log"),
        EFileWrite | EFileShareAny
    );

    if (error == KErrNotFound)
    {
        error = file.Create(
            fs,
            _L("C:\\ngba_startup.log"),
            EFileWrite | EFileShareAny
        );
    }

    if (error == KErrNone)
    {
        TInt size = 0;

        file.Size(size);
        file.Seek(ESeekEnd, size);
        file.Write(aMessage);
        file.Write(_L8("\r\n"));
        file.Flush();
        file.Close();
    }

    fs.Close();
}

}

GLDEF_C TInt E32Main()
{
    WriteStartupLog(_L8("N-GBA: E32Main entered"));

    WriteStartupLog(_L8("N-GBA: Calling GuiWindowServerMain"));

    TInt result = GuiWindowServerMain();

    TBuf8<64> message;

    message.Append(_L8("N-GBA: GuiWindowServerMain returned: "));
    message.AppendNum(result);

    WriteStartupLog(message);

    return result;
}

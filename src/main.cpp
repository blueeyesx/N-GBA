
#include <e32base.h>
#include <e32std.h>

#include "window_server.h"

// Symbian application entry point.
GLDEF_C TInt E32Main()
{
    CTrapCleanup* cleanup = CTrapCleanup::New();

    if (cleanup == NULL)
    {
        return KErrNoMemory;
    }

    TInt result = KErrNone;

    TRAPD(error,
        result = GuiWindowServerMain();
    );

    if (error != KErrNone)
    {
        result = error;
    }

    delete cleanup;

    return result;
}

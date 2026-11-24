
#include <e32base.h>
#include <e32std.h>

#include "window_server.h"

GLDEF_C TInt E32Main()
{
    return static_cast<TInt>(GuiWindowServerMain());
}


#include <thread>

#include "window_server.h"

int main() {
    int result = 0;

    std::thread windowServerThread([&result]() {
        result = GuiWindowServerMain();
    });

    windowServerThread.join();

    return result;
}

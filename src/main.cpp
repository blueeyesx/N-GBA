#include "ngba/app.hpp"
#include <iostream>
int main(int argc, char** argv) {
  ngba::App app;
  if (argc > 1) {
    if (!app.scan_library(argv[1])) { std::cerr << app.status() << "\n"; return 2; }
    for (const auto& game : app.library().games())
      std::cout << game.title << " [" << game.game_code << "] - " << game.path << "\n";
    std::cout << app.status() << "\n";
  } else {
    std::cout << "N-GBA | Stellar Dragon\n";
    std::cout << "Usage: ngba <ROM-directory>\n";
    std::cout << "Backend: " << app.emulator().backend_name() << "\n";
  }
  return 0;
}

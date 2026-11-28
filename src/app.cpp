#include "ngba/app.hpp"
namespace ngba {
App::App() = default;
bool App::scan_library(const std::string& directory) {
  const bool ok=library_.scan(directory);
  status_=ok ? "Library scanned: "+std::to_string(library_.games().size())+" ROM(s)" : library_.error();
  return ok;
}
bool App::launch_game(const std::string& path) {
  if (!emulator_.load_rom(path)) { status_="ROM open failed: backend unavailable or ROM invalid"; return false; }
  emulator_.reset(); status_=std::string("Running with ")+emulator_.backend_name(); return true;
}
void App::handle_key(int key, bool pressed) { (void)input_.event(key,pressed); }
void App::set_grid_columns(int n) { if(n>=2 && n<=5) settings_.grid_columns=n; }
}

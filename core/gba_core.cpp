#include "gba_core.hpp"
#include <fstream>
namespace ngba::core {
GbaCore::GbaCore() : backend_(make_mgba_backend()) {}
bool GbaCore::load_rom(const std::string& path) {
  rom_loaded_ = backend_ && backend_->load_rom(path);
  if (rom_loaded_) rom_path_ = path;
  return rom_loaded_;
}
bool GbaCore::load_bios(const std::string& path) {
  if (!backend_ || !backend_->load_bios(path)) return false;
  bios_path_ = path;
  return true;
}
void GbaCore::reset() { if (backend_) backend_->reset(); }
bool GbaCore::run_frame() { return rom_loaded_ && backend_ && backend_->run_frame(frame_); }
const char* GbaCore::backend_name() const { return backend_ ? backend_->name() : "unavailable"; }
bool GbaCore::save_state(const std::string& path) {
  // State files are deliberately not faked: backend state serialization is still required.
  (void)path; return false;
}
bool GbaCore::load_state(const std::string& path) { (void)path; return false; }
}

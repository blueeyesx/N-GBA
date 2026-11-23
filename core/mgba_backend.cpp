#include "gba_core.hpp"
#include <fstream>
#include <memory>
#ifdef NGBA_HAS_MGBA
// This translation unit is the integration seam. The mGBA public core API is
// intentionally isolated here so platform-specific build profiles can adapt it.
#include <mgba/core/core.h>
#include <mgba/core/interface.h>
#endif
namespace ngba::core {
#ifdef NGBA_HAS_MGBA
// mGBA is fetched as a pinned upstream dependency. Its core API has changed
// between releases; this adapter is kept behind NGBA_HAS_MGBA so CI can test
// the frontend independently. Complete this adapter against the selected
// mGBA release before enabling NGBA_FETCH_MGBA on a device build.
class MgbaBackend final : public EmulatorBackend {
public:
  bool load_rom(const std::string& path) override {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    rom_ = path;
    return false; // fail closed until mGBA VFile/core lifecycle adapter is wired.
  }
  bool load_bios(const std::string& path) override {
    std::ifstream f(path, std::ios::binary);
    return f.good();
  }
  void reset() override {}
  bool run_frame(Frame&) override { return false; }
  const char* name() const override { return "mGBA adapter (not yet activated)"; }
private:
  std::string rom_;
};
#endif
class UnavailableBackend final : public EmulatorBackend {
public:
  bool load_rom(const std::string&) override { return false; }
  bool load_bios(const std::string&) override { return false; }
  void reset() override {}
  bool run_frame(Frame&) override { return false; }
  const char* name() const override { return "core not configured"; }
};
std::unique_ptr<EmulatorBackend> make_mgba_backend() {
#ifdef NGBA_HAS_MGBA
  return std::unique_ptr<EmulatorBackend>(new MgbaBackend());
#else
  return std::unique_ptr<EmulatorBackend>(new UnavailableBackend());
#endif
}
}

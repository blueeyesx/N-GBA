#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace ngba::core {
struct Frame {
  static constexpr int Width=240, Height=160;
  std::vector<uint16_t> pixels{Width*Height, 0};
};
class EmulatorBackend {
public:
  virtual ~EmulatorBackend() = default;
  virtual bool load_rom(const std::string&) = 0;
  virtual bool load_bios(const std::string&) = 0;
  virtual void reset() = 0;
  virtual bool run_frame(Frame&) = 0;
  virtual const char* name() const = 0;
};
class GbaCore {
public:
  GbaCore();
  bool load_rom(const std::string& path);
  bool load_bios(const std::string& path);
  void reset();
  bool run_frame();
  bool save_state(const std::string& path);
  bool load_state(const std::string& path);
  Frame& frame() { return frame_; }
  bool rom_loaded() const { return rom_loaded_; }
  const char* backend_name() const;
private:
  Frame frame_;
  std::unique_ptr<EmulatorBackend> backend_;
  std::string rom_path_, bios_path_;
  bool rom_loaded_{false};
};
std::unique_ptr<EmulatorBackend> make_mgba_backend();
}

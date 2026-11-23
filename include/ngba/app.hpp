#pragma once
#include "ngba/settings.hpp"
#include "ngba/library.hpp"
#include "ngba/input.hpp"
#include "gba_core.hpp"
#include <string>
namespace ngba {
class App {
public:
  App();
  bool scan_library(const std::string& directory);
  bool launch_game(const std::string& path);
  void handle_key(int platform_key, bool pressed);
  void set_grid_columns(int columns);
  const GameLibrary& library() const { return library_; }
  Settings& settings() { return settings_; }
  core::GbaCore& emulator() { return emulator_; }
  std::string status() const { return status_; }
private:
  Settings settings_;
  GameLibrary library_;
  InputMapper input_;
  core::GbaCore emulator_;
  std::string status_{"N-GBA ready"};
};
}

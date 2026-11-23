#pragma once
#include <string>
#include <map>
namespace ngba {
enum class RenderBackend { Interpreter, Dynarec };
enum class Orientation { Auto, Portrait, Landscape, LandscapeFlip };
enum class AspectRatio { Original3x2, FourThree, Square, SixteenNine, Stretch };
enum class LibraryMode { Grid, List };
enum class GbaButton { A, B, Select, Start, Right, Left, Up, Down, R, L };
struct Settings {
  RenderBackend backend{RenderBackend::Interpreter};
  Orientation orientation{Orientation::Auto};
  AspectRatio aspect{AspectRatio::Original3x2};
  LibraryMode library_mode{LibraryMode::Grid};
  int grid_columns{3};
  int scale{2};
  int frameskip{-1};
  int volume{80};
  bool show_fps{true}, show_performance{false}, sound_stereo{true};
  bool vibration{false}, touch_overlay{true};
  std::string rom_directory{"E:/N-GBA/ROMs"};
  std::string bios_path{"E:/N-GBA/BIOS/gba_bios.bin"};
  std::string wallpaper_path;
  std::map<int, GbaButton> keymap;
};
}

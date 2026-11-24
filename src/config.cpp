#include "ngba/config.hpp"
#include <fstream>
namespace ngba {
bool save_settings(const Settings& s, const std::string& path) {
  std::ofstream f(path);
  if (!f) return false;
  f << "grid_columns=" << s.grid_columns << "\nscale=" << s.scale
    << "\nframeskip=" << s.frameskip << "\nvolume=" << s.volume
    << "\nshow_fps=" << s.show_fps << "\nrom_directory=" << s.rom_directory
    << "\nbios_path=" << s.bios_path << "\n";
  return f.good();
}
bool load_settings(Settings& s, const std::string& path) {
  std::ifstream f(path); if (!f) return false;
  std::string line;
  while (std::getline(f,line)) {
    auto p=line.find('='); if (p==std::string::npos) continue;
    auto k=line.substr(0,p), v=line.substr(p+1);
    try {
      if(k=="grid_columns") s.grid_columns=std::stoi(v);
      else if(k=="scale") s.scale=std::stoi(v);
      else if(k=="frameskip") s.frameskip=std::stoi(v);
      else if(k=="volume") s.volume=std::stoi(v);
      else if(k=="show_fps") s.show_fps=(v=="1");
      else if(k=="rom_directory") s.rom_directory=v;
      else if(k=="bios_path") s.bios_path=v;
    } catch (...) { return false; }
  }
  return true;
}
}

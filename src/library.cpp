#include "ngba/library.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
namespace fs = std::filesystem;
namespace ngba {
static std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}
bool GameLibrary::scan(const std::string& directory) {
  games_.clear(); error_.clear();
  std::error_code ec;
  if (!fs::exists(directory, ec) || !fs::is_directory(directory, ec)) {
    error_ = "ROM directory does not exist"; return false;
  }
  for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    const auto path = it->path();
    if (upper(path.extension().string()) != ".GBA") continue;
    GameEntry g;
    g.path = path.string(); g.file_name = path.filename().string();
    g.title = path.stem().string(); g.file_size = it->file_size(ec);
    std::ifstream f(path, std::ios::binary);
    char h[0xB0]{};
    if (f.read(h, sizeof(h))) {
      g.title.assign(h + 0xA0, h + 0xAC);
      while (!g.title.empty() && (g.title.back() == '\0' || g.title.back() == ' ')) g.title.pop_back();
      g.game_code.assign(h + 0xAC, h + 0xB0);
      g.valid_header = true;
    }
    auto cover = path; cover.replace_extension(".png");
    if (fs::exists(cover, ec)) g.cover_path = cover.string();
    games_.push_back(std::move(g));
  }
  std::sort(games_.begin(), games_.end(), [](const GameEntry& a, const GameEntry& b){ return a.title < b.title; });
  if (ec) { error_ = ec.message(); return false; }
  return true;
}
}

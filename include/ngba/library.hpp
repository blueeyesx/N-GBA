#pragma once
#include <string>
#include <vector>
namespace ngba {
struct GameEntry {
  std::string path, file_name, title, game_code;
  unsigned long long file_size{0};
  bool valid_header{false};
  std::string cover_path;
};
class GameLibrary {
public:
  bool scan(const std::string& directory);
  const std::vector<GameEntry>& games() const { return games_; }
  const std::string& error() const { return error_; }
private:
  std::vector<GameEntry> games_;
  std::string error_;
};
}

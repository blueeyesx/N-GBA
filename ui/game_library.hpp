#pragma once
#include <string>
#include <vector>
#include <algorithm>
namespace ngba::ui {
struct GameEntry { std::string rom_path,title,game_code,cover_path; bool favorite{false}; uint64_t last_played{0}; };
enum class LibraryView { Grid2x2, Grid2x3, Grid3x3, Grid3x4, List };
class GameLibrary {
 std::vector<GameEntry> games_; std::string filter_; LibraryView view_{LibraryView::Grid2x3};
public:
 void add(GameEntry e){ games_.push_back(std::move(e)); }
 void set_view(LibraryView v){view_=v;} LibraryView view() const{return view_;}
 void set_filter(std::string f){filter_=std::move(f);}
 std::vector<const GameEntry*> visible() const { std::vector<const GameEntry*> out; for(auto& g:games_) if(filter_.empty()||g.title.find(filter_)!=std::string::npos||g.game_code.find(filter_)!=std::string::npos) out.push_back(&g); return out; }
 void toggle_favorite(const std::string& path){for(auto& g:games_) if(g.rom_path==path) g.favorite=!g.favorite;}
 size_t size() const{return games_.size();}
};
}

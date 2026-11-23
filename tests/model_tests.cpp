#include "ngba/controls.hpp"
#include "game_library.hpp"
#include <cassert>
int main(){ ngba::ControlMap m; assert(m.find(ngba::GbaButton::A)); assert(m.set_key(ngba::GbaButton::A,42)); assert(m.find(ngba::GbaButton::A)->key_code==42); ngba::ui::GameLibrary l; l.add({"a.gba","Harvest Moon","A4NE","cover.png",false,0}); l.add({"b.gba","Mario","ABCD","",false,0}); l.set_filter("Harvest"); assert(l.visible().size()==1); l.toggle_favorite("a.gba"); assert(l.visible()[0]->favorite); }

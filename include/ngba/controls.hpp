#pragma once
#include <array>
#include <cstdint>
#include <string>
namespace ngba {
enum class GbaButton : uint8_t { A, B, L, R, Start, Select, Up, Down, Left, Right };
struct ControlBinding { GbaButton button; int key_code; bool turbo{false}; uint16_t turbo_period_ms{100}; };
struct ControlMap {
 std::array<ControlBinding,10> bindings{{
  {GbaButton::A,0x200},{GbaButton::B,0x201},{GbaButton::L,0x202},{GbaButton::R,0x203},
  {GbaButton::Start,0x204},{GbaButton::Select,0x205},{GbaButton::Up,0x206},{GbaButton::Down,0x207},
  {GbaButton::Left,0x208},{GbaButton::Right,0x209}}};
 const ControlBinding* find(GbaButton b) const { for (const auto& x:bindings) if(x.button==b) return &x; return nullptr; }
 bool set_key(GbaButton b,int key) { for(auto& x:bindings) if(x.button==b){x.key_code=key;return true;} return false; }
};
}

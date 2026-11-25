#include "ngba/input.hpp"
namespace ngba {
InputMapper::InputMapper() {
  // SDL-like symbolic defaults; platform frontends translate native scan codes.
  bindings_[1]=GbaButton::A; bindings_[2]=GbaButton::B;
  bindings_[13]=GbaButton::Start; bindings_[12]=GbaButton::Select;
  bindings_[37]=GbaButton::Left; bindings_[38]=GbaButton::Up;
  bindings_[39]=GbaButton::Right; bindings_[40]=GbaButton::Down;
  bindings_[10]=GbaButton::L; bindings_[11]=GbaButton::R;
}
void InputMapper::bind(int key, GbaButton button) { bindings_[key]=button; }
std::vector<KeyEvent> InputMapper::event(int key, bool pressed) const {
  auto it=bindings_.find(key);
  if (it==bindings_.end()) return {};
  return {{it->second, pressed}};
}
}

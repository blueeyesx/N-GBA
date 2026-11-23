#pragma once
#include "ngba/settings.hpp"
#include <map>
#include <vector>
namespace ngba {
struct KeyEvent { GbaButton button; bool pressed; };
class InputMapper {
public:
  InputMapper();
  void bind(int platform_key, GbaButton button);
  std::vector<KeyEvent> event(int platform_key, bool pressed) const;
  const std::map<int,GbaButton>& bindings() const { return bindings_; }
private:
  std::map<int,GbaButton> bindings_;
};
}

#pragma once

#include "flight/controls.h"
#include "graphics/scene.h"

namespace corncob::game {

struct map_input {
  bool higher{false};
  bool lower{false};
  bool home{false};
  bool rotate{false};
  bool slow{false};
  bool move_object{false};
  bool rotate_object{false};
};

class map_view {
private:
  maths::world_position origin;
  graphics::camera observer;
  maths::motion_fraction fraction;

public:
  explicit map_view(maths::world_position const &position);
  void advance(flight::control_input const &stick, map_input const &input, double ratio = 1.0);
  graphics::camera camera() const;
};

} // namespace corncob::game

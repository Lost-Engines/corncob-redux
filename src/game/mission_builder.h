#pragma once

#include <optional>
#include <string>
#include "game/map_view.h"
#include "resources/geometry_bank.h"

namespace corncob::game {

enum class builder_command {
  cursor, hide_cursor, create, select, erase, next_type, previous_type,
  next_quality, previous_quality, special, complete, level, lock_height, remember_height, map,
};

class mission_builder {
private:
  world::theatre &world;
  resources::geometry_bank const &geometry;
  graphics::camera observer;
  maths::motion_fraction fraction;
  maths::world_position cursor{0, 0, 0};
  std::optional<unsigned int> selected;
  bool cursor_visible{false};
  std::optional<unsigned int> marker;
  void update_marker();
  bool overhead{false};
  int32_t speed{512}, height_lock{0};
  void change_type(int direction);

public:
  mission_builder(world::theatre &world, resources::geometry_bank const &geometry);
  void command(builder_command command);
  void speed_setting(unsigned int setting);
  void advance(flight::control_input const &stick, map_input const &input, bool forward, bool backward, double ratio = 1.0);
  graphics::camera camera() const;
  std::string status() const;
  std::optional<unsigned int> selected_object() const noexcept;
};

} // namespace corncob::game

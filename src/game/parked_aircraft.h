#pragma once

#include "world/world.h"

namespace corncob::game {

enum class parking_reset {
  unchanged,
  enable,
  disable
};

struct aircraft_parking {
  static uint8_t constexpr positioned{1};
  static uint8_t constexpr keep_pose{0x10};
  uint16_t placed_count{0};
  bool resetting{true};
  bool new_arrival{true};
  maths::angles base_attitude{
    .heading{static_cast<maths::angle>(-8000)},
    .pitch{static_cast<maths::angle>(-1500)},
  };
};

parking_reset tower_parking_reset(maths::world_position const &tower, maths::world_position const &observer) noexcept;
bool advance_parked_aircraft(world::placed_object &object, maths::world_position const &base, maths::world_position const &observer,
                             aircraft_parking &parking, bool on_foot, bool exiting = false) noexcept;

} // namespace corncob::game

#pragma once

#include <cstdint>
#include "maths/fixed.h"

namespace corncob::flight {

enum class aircraft_variant {
  standard,
  reserve,
  experimental,
};

struct time_step {
  int16_t elapsed{5};
  int16_t normal{5};
  double seconds{0}; // Zero selects the preserved native arithmetic.
  bool clock_tick{true};
};

struct atmosphere {
  int16_t gravity{-48};
  uint16_t sea_level_density{5957};
  uint16_t height{5957};
  uint16_t power_transition{3971};
};

struct aircraft_state {
  static int16_t constexpr rocket_capacity{2184};
  maths::world_position position{0, 0, 720};
  maths::angles attitude;
  maths::rotation world_to_body{maths::make_rotation({})};
  maths::rotation body_to_world{maths::make_inverse_rotation({})};
  maths::fixed_vector velocity{0, 0, 0};
  maths::fixed_vector body_velocity{0, 0, 0};
  maths::fixed_vector scaled_body_velocity{0, 0, 0};
  maths::fixed_vector acceleration{0, 0, 0};
  maths::fixed_vector body_acceleration{0, 0, 0};
  vec3<int32_t> body_force{0, 0, 0};
  aircraft_variant variant{aircraft_variant::standard};
  uint16_t throttle{20};
  uint16_t engine_temperature{0};
  uint16_t oil_pressure{0};
  uint16_t density{5957};
  uint16_t power_factor{65'535};
  int16_t angle_of_attack{0};
  int16_t previous_angle_of_attack{0};
  int16_t side_force{0};
  int16_t last_vertical_axis{0};
  uint16_t rolls{0};
  int16_t rocket_ticks{rocket_capacity};
  bool rocket{false};
  bool flaps{false};
  bool on_ground{false};
  bool stalled{false};
};

void advance_forces(aircraft_state &aircraft, atmosphere const &environment, time_step step);
void advance_position(aircraft_state &aircraft, time_step step);

} // namespace corncob::flight

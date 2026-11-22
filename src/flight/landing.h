#pragma once

#include <cstdint>
#include "vectorstorm/vector/vector2.h"
#include "flight/controls.h"

namespace corncob::flight {

enum class landing_result {
  airborne,
  contact,
  stopped,
  crash_landing,
  fatal,
};

struct landing_state {
  static int16_t constexpr aircraft_height{720};
  static int16_t constexpr half_wheel_span{1560};
  int32_t surface_height{0};
  vec2<int16_t> wheel_heights{aircraft_height, aircraft_height};
  int16_t clearance{aircraft_height};
  int16_t spring_compression{0};
  int16_t previous_vertical_speed{0};
  bool runway{false};
  bool stopped{true};
  bool crash_landing{false};
};

void recover_invulnerable_aircraft(aircraft_state &aircraft, landing_state &landing) noexcept;

bool find_wheel_height(aircraft_state const &aircraft, landing_state &landing) noexcept;
landing_result advance_landing(aircraft_state &aircraft, angular_response &response, landing_state &landing,
                               control_input const &input, time_step step);

} // namespace corncob::flight

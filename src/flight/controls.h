#pragma once

#include <cstdint>
#include "maths/random.h"
#include "flight/aircraft.h"

namespace corncob::flight {

struct control_input {
  int16_t roll{0};
  int16_t pitch{0};
  int16_t rudder{0};
  bool brakes{false};
};

struct autopilot_state {
  bool engaged{false};
  bool height_locked{false};
  int32_t height{0};
  int16_t previous_vertical_airflow{0};
};

struct angular_response {
  int32_t roll{0};
  int32_t pitch{0};
  int32_t yaw{0};
  int16_t wing_damage{0};
  int16_t rudder_damage{0};
  int16_t stability_damage{0};
  uint16_t almost_stall_angle{2548};
  uint16_t last_protected_pitch_rate{0};
  bool stall_protection{false};
  bool beginner_flight{false};
  autopilot_state autopilot{};
};

void advance_autopilot(aircraft_state const &aircraft, angular_response &response);

void advance_controls(aircraft_state &aircraft, angular_response &response, control_input const &input,
                      atmosphere const &environment, time_step step, maths::random_source *random = nullptr);

} // namespace corncob::flight

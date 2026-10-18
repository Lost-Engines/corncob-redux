#pragma once

#include "flight/controls.h"
#include "maths/fixed.h"
#include "vectorstorm/vector/vector2.h"

namespace corncob::game {

struct person_state {
  maths::world_position position{0, 0, 720};
  maths::angles attitude;
  maths::fixed_vector velocity{0, 0, 0};
  int32_t surface_height{0};
  uint16_t jump_charge{0};
  vec2<int32_t> walking_remainder{0, 0};
  uint16_t last_step_tick{0};
  int16_t step_wobble{100};
  int16_t horizontal_damping{32'000};
  bool standing{true};
  bool parachute{false};
  bool charging_jump{false};
  maths::angles ejection_attitude;
  vec2<int16_t> tumble{0, 0};
  uint16_t swing_frames{0};
};

maths::world_position walking_displacement(maths::angle heading, maths::fixed_vector const &stride, flight::time_step step);
void damp_fall_velocity(maths::fixed_vector &velocity, int16_t horizontal_damping, int16_t terminal_velocity, flight::time_step step);
void swing_parachute(person_state &person, flight::control_input const &input);

} // namespace corncob::game

#pragma once

#include "maths/random.h"
#include "flight/aircraft.h"
#include "flight/controls.h"

namespace corncob::flight {

struct damage_state {
  uint16_t total{0};
  uint16_t worst{0};
  int16_t wing{0};
  int16_t rudder{0};
  int16_t stability{0};
  int16_t wing_loss{0};
  uint16_t engine{0};
  uint16_t maximum_throttle{200};
  bool invincible{false};
  std::array<uint16_t, 4> indicator_hits{};
};

void apply_damage(damage_state &damage, aircraft_state &aircraft, unsigned int events, maths::random_source &random, time_step step);

} // namespace corncob::flight

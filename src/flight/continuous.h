#pragma once

#include "flight/landing.h"
#include "maths/continuous.h"

namespace corncob::flight {

struct continuous_state {
  maths::motion_fraction motion;
  vec3<double> response{0,0,0};
  vec3<double> acceleration{0,0,0};
  double rocket_fraction{0};
  double previous_step{0};
  double pressure_fraction{0}, temperature_fraction{0};
};
void controls_elapsed(aircraft_state &aircraft, angular_response &response, continuous_state &state,
                      control_input const &input, atmosphere const &environment, double seconds,
                      maths::random_source *random);
void forces_elapsed(aircraft_state &aircraft, continuous_state &state, atmosphere const &environment, double seconds);
landing_result landing_elapsed(aircraft_state &aircraft, angular_response &response, landing_state &landing,
                               continuous_state &state, control_input const &input, double seconds);
void position_elapsed(aircraft_state &aircraft, continuous_state &state, double seconds);

} // namespace corncob::flight

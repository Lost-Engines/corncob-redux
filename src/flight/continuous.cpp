#include "continuous.h"
#include <array>
#include <cmath>
#include "game/timing.h"

namespace corncob::flight {

void controls_elapsed(aircraft_state &aircraft, angular_response &response, continuous_state &state,
                      control_input const &input, atmosphere const &environment, double const seconds,
                      maths::random_source *const random) {
  double const ratio{seconds / game::timing::seconds_per_step};
  // Evaluate the original torque/assistance laws at their reference interval.
  // Integrate their rates over actual elapsed time, including exponential inertia.
  auto evaluation{aircraft}; auto next{response};
  if(state.previous_step > 0) {
    double const previous_ratio{state.previous_step/game::timing::seconds_per_step};
    // These feedback laws predict a reference interval ahead, rather than one host callback.
    double const airflow_change{maths::word(aircraft.body_velocity.z-response.autopilot.previous_vertical_airflow)/previous_ratio};
    next.autopilot.previous_vertical_airflow = maths::word(static_cast<int32_t>(aircraft.body_velocity.z)
      - static_cast<int32_t>(std::clamp(airflow_change,-150.0,150.0)));
    double const angle_change{maths::word(aircraft.angle_of_attack-aircraft.previous_angle_of_attack)/previous_ratio};
    evaluation.previous_angle_of_attack = maths::word(maths::dword(static_cast<int64_t>(aircraft.angle_of_attack)
      - static_cast<int64_t>(std::clamp(angle_change,-32768.0,32767.0))));
  }
  advance_controls(evaluation, next, input, environment, {}, nullptr);
  vec3<double> impulse{0,0,0};
  if(random && response.stability_damage != 0) {
    int16_t const airflow{std::clamp<int16_t>(aircraft.scaled_body_velocity.x,-300,300)};
    uint16_t const span{static_cast<uint16_t>(std::abs(static_cast<int>(maths::word(static_cast<int32_t>(response.stability_damage)*airflow/300)))+2)};
    // Damage jitter is one reference-clock impulse, rather than a force scaled by callback duration.
    for(unsigned int axis : {1u,2u,0u}) impulse[axis] = maths::word(random->below(span)-span/2)*65536.0;
  }
  std::array<int32_t*,3> const output{&response.roll, &response.pitch, &response.yaw};
  std::array<int32_t,3> const target{next.roll, next.pitch, next.yaw};
  std::array<double,3> constexpr retained{0.8125, 0.9, 0.9};
  vec3<double> integrated{0,0,0};
  for(unsigned int axis{0}; axis < 3; ++axis) {
    double const before{std::clamp(*output[axis]+state.response[axis]+impulse[axis],-196608000.0,196608000.0)};
    double const forcing{target[axis] - retained[axis] * *output[axis]};
    double const equilibrium{forcing / (1-retained[axis])};
    double const attenuation{maths::decay(retained[axis], ratio)};
    double const after{std::clamp(equilibrium + (before-equilibrium)*attenuation, -196608000.0, 196608000.0)};
    integrated[axis] = (equilibrium*ratio + (before-equilibrium)*(attenuation-1)/std::log(retained[axis])) / 65536.0;
    *output[axis] = static_cast<int32_t>(std::trunc(after));
    state.response[axis] = after - *output[axis];
  }
  response.almost_stall_angle = next.almost_stall_angle;
  response.last_protected_pitch_rate = next.last_protected_pitch_rate;
  response.stall_protection = next.stall_protection;
  response.autopilot = next.autopilot;
  state.previous_step = seconds;
  maths::rotate_elapsed(aircraft.attitude, state.motion.angles, {integrated.z, -integrated.y, integrated.x});
}

void forces_elapsed(aircraft_state &aircraft, continuous_state &state, atmosphere const &environment, double const seconds) {
  double const ratio{seconds / game::timing::seconds_per_step};
  auto const rotation{maths::rotation_real(maths::real_angles(aircraft.attitude, state.motion.angles))};
  auto const velocity{vec3<double>{aircraft.velocity} + state.motion.velocity};
  auto const body{rotation.inverse(velocity)};
  auto evaluated{aircraft};
  // The reference routine supplies force laws and telemetry; its rounded integration is replaced below.
  advance_forces(evaluated, environment, {});
  int16_t const previous_rocket{aircraft.rocket_ticks};
  bool const rocket{aircraft.rocket};
  aircraft = evaluated;
  if(rocket) {
    double const remaining{previous_rocket + state.rocket_fraction - 5*ratio};
    aircraft.rocket_ticks = static_cast<int16_t>(std::max(0.0, std::trunc(remaining)));
    state.rocket_fraction = std::max(0.0, remaining) - aircraft.rocket_ticks;
    aircraft.rocket = remaining > 0;
  }
  vec3<double> acceleration{aircraft.body_force};
  acceleration /= 65536.0;
  auto next{body + acceleration*ratio};
  if(aircraft.on_ground) next.y = 0;
  else next.y = std::clamp(next.y, -9500.0, 9500.0);
  next.z = std::clamp(next.z, -9600.0, 9600.0);
  auto const world{rotation.apply(next)};
  state.acceleration = rotation.apply(acceleration);
  for(unsigned int axis{0}; axis < 3; ++axis) {
    aircraft.velocity[axis] = maths::word(maths::dword(static_cast<int64_t>(std::trunc(world[axis]))));
    state.motion.velocity[axis] = world[axis] - std::trunc(world[axis]);
    aircraft.body_velocity[axis] = maths::word(maths::dword(static_cast<int64_t>(std::trunc(next[axis]))));
    aircraft.scaled_body_velocity[axis] = static_cast<int16_t>(aircraft.body_velocity[axis] >> 4);
    aircraft.acceleration[axis] = maths::word(maths::dword(static_cast<int64_t>(std::trunc(state.acceleration[axis]*ratio))));
  }
}

landing_result landing_elapsed(aircraft_state &aircraft, angular_response &response, landing_state &landing,
                               continuous_state &state, control_input const &input, double const seconds) {
  double const ratio{seconds / game::timing::seconds_per_step};
  auto evaluated{aircraft}; auto spring{response};
  auto const result{advance_landing(evaluated, spring, landing, input, {})};
  aircraft.on_ground = evaluated.on_ground;
  if(result == landing_result::airborne || result == landing_result::fatal || result == landing_result::crash_landing) return result;
  for(unsigned int axis{0}; axis < 3; ++axis) {
    double const delta{static_cast<double>(evaluated.velocity[axis]) - aircraft.velocity[axis]};
    aircraft.velocity[axis] = maths::word(maths::dword(static_cast<int64_t>(aircraft.velocity[axis]) + maths::accumulate(delta*ratio, state.motion.velocity[axis])));
  }
  response.roll = maths::dword(static_cast<int64_t>(response.roll) + maths::accumulate((static_cast<double>(spring.roll)-response.roll)*ratio, state.response.x));
  if(result == landing_result::stopped) {
    aircraft.velocity.x = aircraft.velocity.y = 0;
    state.motion.velocity.x = state.motion.velocity.y = 0;
  }
  return result;
}

void position_elapsed(aircraft_state &aircraft, continuous_state &state, double const seconds) {
  double const ratio{seconds / game::timing::seconds_per_step};
  double const divisor{aircraft.variant == aircraft_variant::experimental ? 4.0 : 16.0};
  auto const velocity{vec3<double>{aircraft.velocity} + state.motion.velocity};
  auto const distance{(velocity - state.acceleration*(0.5*ratio)) * (ratio/divisor)};
  maths::move(aircraft.position, distance, state.motion.position);
}

} // namespace corncob::flight

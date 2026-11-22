#include "aircraft.h"
#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdlib>
#include "maths/integer.h"

namespace corncob::flight {

namespace {

int32_t adjust_time(int32_t const value, time_step const step) {
  /// DWTADJ scales the accumulated fixed-point quantity before extracting its high word
  return maths::rounded_divide(maths::scale(value, step.elapsed), step.normal);
}
int32_t signed_square(int16_t const value) noexcept {
  /// Drag preserves the velocity sign while squaring its magnitude
  return static_cast<int32_t>(value) * std::abs(static_cast<int>(value));
}

} // anonymous namespace

void advance_forces(aircraft_state &aircraft, atmosphere const &environment, time_step const step) {
  /// Translate 3.42's force pass, preserving every intermediate word/dword scale and rounding point
  int16_t constexpr force_scale{16};
  int16_t constexpr engine_coefficient{450};
  int16_t constexpr reserve_engine_coefficient{350};
  int16_t constexpr full_throttle{200};
  int16_t constexpr idle_throttle{20};
  int16_t constexpr minimum_thrust_airflow{300};
  int16_t constexpr drag_breakpoint{1390};
  int16_t constexpr normal_drag{13'734};
  int16_t constexpr fast_drag{1368};
  int16_t constexpr fast_side_drag{136};
  int16_t constexpr normal_lift{2016};
  int16_t constexpr stalled_lift{28'224};
  int16_t constexpr stall_half_angle{2912};
  int16_t constexpr flap_stall_half_angle{3882};
  int constexpr maximum_side_velocity{9500};
  int constexpr maximum_vertical_velocity{9600};
  int constexpr sideslip_snap{200};
  unsigned int constexpr airflow_scale_bits{4};
  int constexpr roll_count_threshold{13'000};
  unsigned int constexpr power_phase_range{20'383};
  unsigned int constexpr power_phase_bias{4000};
  unsigned int constexpr quarter_turn{16'384};
  int16_t constexpr rocket_drain{5};
  int16_t constexpr rocket_engine_multiplier{3};
  assert(environment.height != 0 && environment.power_transition != 0);
  assert(step.normal > 0 && step.elapsed > 0);
  aircraft.body_velocity = maths::transform(aircraft.world_to_body, aircraft.velocity);
  for(unsigned int axis{0}; axis != 3; ++axis) {
    aircraft.scaled_body_velocity[axis] = static_cast<int16_t>(aircraft.body_velocity[axis] >> airflow_scale_bits);
    aircraft.body_force[axis] = static_cast<int32_t>(aircraft.world_to_body.rows[axis].z) * environment.gravity;
  }
  int16_t const vertical_axis{aircraft.world_to_body.rows[2].z};
  if(std::abs(static_cast<int>(vertical_axis >> 1)) >= roll_count_threshold && maths::word(vertical_axis ^ aircraft.last_vertical_axis) < 0) {
    aircraft.last_vertical_axis = vertical_axis;
    ++aircraft.rolls;
  }
  // thrust intentionally uses the previous pass's density, before this pass updates it
  int16_t const density_above_transition{maths::word(aircraft.density - maths::word(environment.sea_level_density - environment.power_transition))};
  aircraft.power_factor = 0;
  if(density_above_transition >= 0) {
    aircraft.power_factor = 65'535;
    if(static_cast<uint16_t>(density_above_transition) <= environment.power_transition) {
      unsigned int const phase{static_cast<unsigned int>(environment.power_transition - density_above_transition) * power_phase_range / environment.power_transition};
      aircraft.power_factor = maths::sine_magnitude(static_cast<maths::angle>(phase - power_phase_bias + quarter_turn));
    }
  }
  auto const engine{aircraft.variant == aircraft_variant::reserve ? reserve_engine_coefficient : engine_coefficient};
  int32_t thrust{maths::rounded_divide(static_cast<uint16_t>(aircraft.throttle - idle_throttle) * engine, full_throttle)};
  if(aircraft.rocket) {
    int16_t const drain{maths::word(static_cast<int32_t>(rocket_drain) * step.elapsed / step.normal)};
    aircraft.rocket_ticks = maths::word(aircraft.rocket_ticks - drain);
    int16_t remaining{drain};
    if(aircraft.rocket_ticks < 0) {
      aircraft.rocket = false;
      aircraft.rocket_ticks = 0;
      remaining = 0;
    }
    // the original rocket branch replaces engine thrust with the timed drain plus three engine coefficients
    thrust = maths::word(remaining + rocket_engine_multiplier * engine_coefficient);
  }
  thrust = maths::dword(static_cast<int64_t>(static_cast<uint16_t>(thrust)) * aircraft.power_factor);
  thrust = maths::rounded_divide(thrust, std::max(aircraft.scaled_body_velocity.x, minimum_thrust_airflow));
  aircraft.body_force.x = maths::dword(static_cast<int64_t>(aircraft.body_force.x) + maths::scale(thrust, force_scale));

  int const height{std::max(0, static_cast<int>(maths::word(aircraft.position.z >> 8)))};
  int16_t const remaining_height{maths::word(environment.height - height)};
  aircraft.density = static_cast<uint16_t>(static_cast<uint32_t>(environment.sea_level_density)
    * static_cast<uint16_t>(std::max<int16_t>(remaining_height, 0)) / environment.height);
  int16_t const reduced_density{static_cast<int16_t>(maths::word(aircraft.density) >> airflow_scale_bits)};
  for(unsigned int axis{0}; axis != 2; ++axis) {
    int16_t const airflow{aircraft.scaled_body_velocity[axis]};
    int16_t divisor{maths::word(airflow - drag_breakpoint) < 0 ? normal_drag : axis == 0 ? fast_drag : fast_side_drag};
    if(axis == 0 && aircraft.flaps) divisor = static_cast<int16_t>(divisor >> 2);
    int32_t drag{maths::scale(signed_square(airflow), reduced_density)};
    drag = maths::scale(maths::rounded_divide(drag, divisor), -force_scale);
    aircraft.body_force[axis] = maths::dword(static_cast<int64_t>(aircraft.body_force[axis]) + drag);
    if(axis == 1) {
      uint32_t const magnitude{static_cast<uint32_t>(std::abs(static_cast<int64_t>(drag)))};
      aircraft.side_force = (magnitude & 0x8000u) != 0 ? static_cast<int16_t>(drag < 0 ? -32'768 : 32'767) : maths::word(drag);
    }
  }
  if(aircraft.on_ground) {
    aircraft.body_force.y = 0;
    aircraft.body_velocity.y = 0;
    aircraft.side_force = 0;
  }
  aircraft.previous_angle_of_attack = aircraft.angle_of_attack;
  aircraft.angle_of_attack = std::bit_cast<int16_t>(maths::arctangent(aircraft.body_velocity.x, aircraft.body_velocity.z));
  int16_t const half_angle{aircraft.flaps ? flap_stall_half_angle : stall_half_angle};
  int16_t const shifted_angle{maths::word(aircraft.angle_of_attack + half_angle)};
  aircraft.stalled = shifted_angle < 0 || maths::word(shifted_angle - half_angle * 2) >= 0;
  int16_t const lift_divisor{aircraft.stalled ? stalled_lift : static_cast<int16_t>(aircraft.flaps ? normal_lift / 2 : normal_lift)};
  int32_t lift{maths::rounded_divide(-signed_square(aircraft.body_velocity.z), 16)};
  lift = maths::rounded_divide(lift, lift_divisor);
  lift = maths::scale(lift, maths::word(aircraft.density));
  lift = maths::scale(lift, force_scale);
  aircraft.body_force.z = maths::dword(static_cast<int64_t>(aircraft.body_force.z) + lift);
  for(unsigned int axis{0}; axis != 3; ++axis) {
    aircraft.body_acceleration[axis] = maths::word(adjust_time(aircraft.body_force[axis], step) >> 16);
  }
  aircraft.body_velocity.x = maths::word(aircraft.body_velocity.x + aircraft.body_acceleration.x);
  if(aircraft.body_acceleration.y == 0 && std::abs(static_cast<int>(aircraft.body_velocity.y)) <= sideslip_snap) aircraft.body_velocity.y = 0;
  aircraft.body_velocity.y = static_cast<int16_t>(std::clamp(static_cast<int>(maths::word(aircraft.body_velocity.y + aircraft.body_acceleration.y)), -maximum_side_velocity, maximum_side_velocity));
  aircraft.body_velocity.z = static_cast<int16_t>(std::clamp(static_cast<int>(maths::word(aircraft.body_velocity.z + aircraft.body_acceleration.z)), -maximum_vertical_velocity, maximum_vertical_velocity));
  aircraft.acceleration = maths::transform(aircraft.body_to_world, aircraft.body_acceleration);
  aircraft.velocity = maths::transform(aircraft.body_to_world, aircraft.body_velocity);
}

void advance_position(aircraft_state &aircraft, time_step const step) {
  /// MOVEME integrates velocity plus half acceleration; the experimental aircraft advances four times farther
  unsigned int constexpr normal_position_shift{4};
  unsigned int constexpr experimental_position_shift{2};
  unsigned int const shift{aircraft.variant == aircraft_variant::experimental ? experimental_position_shift : normal_position_shift};
  for(unsigned int axis{0}; axis != 3; ++axis) {
    int16_t const velocity{maths::word((aircraft.acceleration[axis] >> 1) + aircraft.velocity[axis])};
    int32_t const distance{adjust_time(velocity, step) >> shift};
    aircraft.position[axis] = maths::dword(static_cast<int64_t>(aircraft.position[axis]) + distance);
  }
}

} // namespace corncob::flight

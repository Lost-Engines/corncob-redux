#include "landing.h"
#include <algorithm>
#include <cstdlib>
#include "maths/integer.h"

namespace corncob::flight {

void recover_invulnerable_aircraft(aircraft_state &aircraft, landing_state &landing) noexcept {
  /// RESETPLANE levels the aircraft at hmin; BOUNCEVERT reverses descent with speed-dependent word damping
  if(aircraft.velocity.z < 0) {
    int16_t const upward{maths::negate(aircraft.velocity.z)};
    int16_t const damping{static_cast<int16_t>(upward >> (static_cast<uint16_t>(aircraft.body_velocity.x) < 3000 ? 1 : 3))};
    aircraft.velocity.z = maths::word(upward - damping);
  }
  aircraft.position.z = landing_state::aircraft_height;
  aircraft.attitude.pitch = aircraft.attitude.roll = 0;
  aircraft.world_to_body = maths::make_rotation(aircraft.attitude);
  aircraft.body_to_world = maths::make_inverse_rotation(aircraft.attitude);
  aircraft.on_ground = false;
  landing.crash_landing = false;
  landing.stopped = false;
}

bool find_wheel_height(aircraft_state const &aircraft, landing_state &landing) noexcept {
  /// FINDHEIGHT transforms both wheel contacts and rejects extreme pitch/roll close to the ground
  int constexpr maximum_landing_angle{10'000};
  if(std::abs(static_cast<int>(std::bit_cast<int16_t>(aircraft.attitude.roll))) > maximum_landing_angle
     || std::abs(static_cast<int>(std::bit_cast<int16_t>(aircraft.attitude.pitch))) > maximum_landing_angle) return false;
  for(unsigned int wheel{0}; wheel != 2; ++wheel) {
    maths::fixed_vector const contact{0, static_cast<int16_t>(wheel == 0 ? landing_state::half_wheel_span : -landing_state::half_wheel_span), -landing_state::aircraft_height};
    landing.wheel_heights[wheel] = maths::negate(maths::transform(aircraft.body_to_world, contact).z);
  }
  landing.clearance = std::max(landing.wheel_heights.x, landing.wheel_heights.y);
  return true;
}

landing_result advance_landing(aircraft_state &aircraft, angular_response &response, landing_state &landing,
                               control_input const &input, time_step const step) {
  /// Translate the undamped suspension used by the 3.42 binary, including its double lateral brake application
  int constexpr spring_rest{90};
  int constexpr maximum_compression{540};
  int constexpr spring_coefficient{266};
  int constexpr maximum_grass_compression{maximum_compression / 2};
  if(landing.crash_landing) return landing_result::crash_landing;
  landing.spring_compression = 0;
  aircraft.on_ground = false;
  landing.previous_vertical_speed = maths::word(static_cast<int32_t>(aircraft.velocity.z) * step.normal / step.elapsed);
  int32_t const altitude{maths::dword(static_cast<int64_t>(aircraft.position.z) - landing.surface_height)};
  if(altitude < 0 || altitude >= 65'536) return landing_result::airborne;
  int32_t const compression{static_cast<uint16_t>(landing.clearance + spring_rest) - altitude};
  if(compression < 0) return landing_result::airborne;
  int const failure_compression{landing.runway ? compression : compression * 2};
  if((landing.runway && compression >= maximum_compression) || (!landing.runway && compression >= maximum_grass_compression)) {
    if(failure_compression >= maximum_compression * 2) return landing_result::fatal;
    landing.crash_landing = true;
    return landing_result::crash_landing;
  }
  aircraft.on_ground = true;
  aircraft.body_velocity.y = 0;
  aircraft.velocity = maths::transform(aircraft.body_to_world, aircraft.body_velocity);
  landing.spring_compression = maths::word(compression);
  int16_t const spring_impulse{maths::word(compression * spring_coefficient / 1000)};
  int16_t const vertical_unscaled{maths::word(static_cast<int32_t>(aircraft.velocity.z) * step.normal / step.elapsed)};
  int16_t const combined{maths::word(vertical_unscaled + spring_impulse)};
  aircraft.velocity.z = maths::word(static_cast<int32_t>(combined) * step.elapsed / step.normal);
  int16_t const wheel_difference{maths::word(landing.wheel_heights.x - landing.wheel_heights.y)};
  response.roll = maths::dword(static_cast<int64_t>(response.roll) + static_cast<int64_t>(wheel_difference) * landing.spring_compression * 32);
  if(input.brakes || aircraft.throttle <= 40) {
    uint16_t const speed{maths::magnitude(aircraft.velocity)};
    int16_t const strength{maths::word((input.brakes ? 15 : 6) * step.elapsed / step.normal)};
    int16_t const change_x{maths::word(static_cast<int32_t>(strength) * maths::negate(aircraft.velocity.x) / static_cast<int16_t>(speed))};
    int16_t const change_y{maths::word(static_cast<int32_t>(strength) * maths::negate(aircraft.velocity.y) / static_cast<int16_t>(speed))};
    aircraft.velocity.x = maths::word(aircraft.velocity.x + change_x);
    aircraft.velocity.y = maths::word(aircraft.velocity.y + change_y * 2);
    if(speed <= static_cast<uint16_t>(100 * step.elapsed / step.normal) && aircraft.throttle <= 40) {
      aircraft.velocity.x = 0;
      aircraft.velocity.y = 0;
      landing.stopped = true;
      return landing_result::stopped;
    }
  }
  return landing_result::contact;
}

} // namespace corncob::flight

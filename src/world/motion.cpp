#include "motion.h"
#include <cstdlib>
#include <cmath>
#include "game/timing.h"
#include "maths/integer.h"

namespace corncob::world {

bool advance_motion(placed_object &object, bool const rotates, maths::random_source &random, flight::time_step const step) {
  /// OBJMOV advances transient objects in centimetres per tick, without the aircraft's extra divide by sixteen
  if(step.seconds > 0) {
    double const ratio{step.seconds / game::timing::seconds_per_step};
    if(rotates) {
      maths::turn(object.attitude.heading, object.motion.spin.x*ratio, object.fraction.angles.x);
      maths::turn(object.attitude.pitch, object.motion.spin.y*ratio, object.fraction.angles.y);
      maths::turn(object.attitude.roll, object.motion.spin.z*ratio, object.fraction.angles.z);
    }
    vec3<double> velocity{vec3<double>{object.velocity} + object.fraction.velocity};
    vec3<double> distance{0,0,0};
    for(unsigned int axis{0}; axis < 3; ++axis) {
      auto const dispersion{static_cast<uint16_t>(object.motion.dispersion)};
      if(step.clock_tick && dispersion != 0) velocity[axis] += maths::word(random.below(dispersion) - (dispersion+1)/2);
      double const before{velocity[axis]};
      if(object.motion.drag_divisor != 0) velocity[axis] /= 1 + std::abs(velocity[axis])*ratio/static_cast<uint16_t>(object.motion.drag_divisor);
      unsigned int const gravity{axis == 2 ? static_cast<uint16_t>(object.motion.gravity)&255u : 0u};
      if(gravity != 55) velocity[axis] -= gravity*ratio;
      distance[axis] = ((before+velocity[axis])*0.5 - (gravity == 55 ? 55 : 0))*ratio;
      object.velocity[axis] = maths::word(maths::dword(static_cast<int64_t>(std::trunc(velocity[axis]))));
      object.fraction.velocity[axis] = velocity[axis] - std::trunc(velocity[axis]);
    }
    maths::move(object.position, distance, object.fraction.position);
    if(object.position.z >= 0 || object.velocity.z == 0) return false;
    object.position.z = 0; object.fraction.position.z = 0;
    object.velocity = maths::fixed_vector{0,0,0}; object.fraction.velocity = vec3<double>{0,0,0};
    if(rotates) object.motion = {};
    return true;
  }
  if(rotates) {
    object.attitude.heading = static_cast<maths::angle>(object.attitude.heading + object.motion.spin.x);
    object.attitude.pitch = static_cast<maths::angle>(object.attitude.pitch + object.motion.spin.y);
    object.attitude.roll = static_cast<maths::angle>(object.attitude.roll + object.motion.spin.z);
  }
  for(unsigned int axis{0}; axis != 3; ++axis) {
    int16_t drift{0};
    auto const dispersion{static_cast<uint16_t>(object.motion.dispersion)};
    if(dispersion != 0) drift = maths::word(random.below(dispersion) - (dispersion + 1) / 2);
    if(object.motion.drag_divisor != 0) {
      object.velocity[axis] = maths::word(object.velocity[axis] + drift);
      int32_t const velocity{object.velocity[axis]};
      auto const drag{static_cast<uint32_t>(velocity * velocity) / static_cast<uint16_t>(object.motion.drag_divisor)};
      drift = maths::word(velocity >= 0 ? -static_cast<int32_t>(drag) : static_cast<int32_t>(drag));
    }
    object.velocity[axis] = maths::word(object.velocity[axis] + drift);
    int16_t displacement{object.velocity[axis]};
    if(axis == 2) {
      // gravity's upper byte is an expiry-explosion flag, not part of the acceleration
      int16_t gravity{static_cast<int16_t>(static_cast<uint16_t>(object.motion.gravity) & 255u)};
      bool const constant_descent{gravity == 55};
      if(!constant_descent) gravity = maths::word(static_cast<int32_t>(gravity) * step.elapsed / step.normal);
      displacement = maths::word(displacement - gravity);
      // 55 is the original constant-descent convention: it does not accumulate in velocity
      if(!constant_descent) object.velocity.z = displacement;
    }
    object.position[axis] = maths::dword(static_cast<int64_t>(object.position[axis])
      + maths::rounded_divide(static_cast<int32_t>(displacement) * step.elapsed, step.normal));
  }
  if(object.position.z >= 0 || object.velocity.z == 0) return false;
  object.position.z = 0;
  object.velocity = maths::fixed_vector{0, 0, 0};
  if(rotates) object.motion = {};
  return true;
}

bool boxes_overlap(maths::world_position const &a, maths::fixed_vector const &a_extents,
                   maths::world_position const &b, maths::fixed_vector const &b_extents) noexcept {
  /// CHKBOX uses strict overlap, unsigned stored half-extents, and zero X extent to disable an object
  if(a_extents.x == 0 || b_extents.x == 0) return false;
  for(unsigned int axis{0}; axis != 3; ++axis) {
    auto const extent{static_cast<uint32_t>(static_cast<uint16_t>(a_extents[axis])) + static_cast<uint16_t>(b_extents[axis])};
    auto const difference{maths::dword(static_cast<int64_t>(a[axis]) - b[axis])};
    if(static_cast<uint64_t>(std::abs(static_cast<int64_t>(difference))) >= extent) return false;
  }
  return true;
}

} // namespace corncob::world

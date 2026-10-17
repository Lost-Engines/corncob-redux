#include "vehicle.h"
#include <algorithm>
#include <cstdlib>
#include "maths/integer.h"
#include "resources/actor_tables.h"

namespace corncob::game {

unsigned int vehicle_tier(world::placed_object const &object, maths::world_position const &observer) noexcept {
  /// Vehicle pursuit and speed use the renderer's component-based distance tier
  auto const delta{vec3<int64_t>{object.position} - vec3<int64_t>{observer}};
  int64_t const maximum{std::max({std::abs(delta.x), std::abs(delta.y), std::abs(delta.z)})};
  return maximum < 32'768 ? 0u : maximum < 524'288 ? 1u : maximum < 8'388'608 ? 2u : 3u;
}
void follow_vehicle(world::placed_object &object, unsigned int const tier) {
  /// FOLLOWANGLES selects near/far speed and the two successive capture slowdown flags
  auto const &parameters{resources::vehicle_qualities[object.quality()]};
  unsigned int speed{tier == 0 ? parameters.near_speed : parameters.far_speed};
  if(tier == 0 && (object.flags & 2u) != 0) {
    speed /= 4;
    if((object.flags & 4u) != 0) speed = 0;
  }
  object.velocity = maths::transform(maths::make_inverse_rotation(object.attitude), {maths::word(speed), 0, 0});
}
void reset_vehicle(world::placed_object &object, maths::world_position const &observer, unsigned int const tier, bool const on_foot, maths::random_source &random, vehicle_effects &effects) {
  /// RESETVECH is shared by ordinary direction changes and surviving or rejected hits
  if((object.flags & 4u) == 0) {
    object.attitude.pitch = object.attitude.roll = 0;
    object.attitude.heading = random.next();
  }
  auto const &parameters{resources::vehicle_qualities[object.quality()]};
  unsigned int const pursuit{tier == 1 ? parameters.near_pursuit_threshold : parameters.far_pursuit_threshold};
  if(tier == 0 || random.next() <= pursuit) {
    maths::fixed_vector direction{0, 0, 0};
    bool local{true};
    for(unsigned int axis{0}; axis != 3; ++axis) {
      int32_t const delta{maths::dword(static_cast<int64_t>(object.position[axis]) - observer[axis])};
      if(std::abs(static_cast<int64_t>(delta)) >= 524'288) local = false;
      direction[axis] = maths::negate(maths::word(delta >> 6));
    }
    if(local) {
      if((object.flags & 4u) == 0) object.attitude.heading = static_cast<maths::angle>(maths::arctangent(maths::negate(direction.x), maths::negate(direction.y)) + 32'768);
      uint16_t const distance{maths::magnitude(direction)};
      if(on_foot && distance <= 256) {
        if(!effects.capture_pending) {
          effects.capture_pending = true;
          object.flags |= 2u;
        }
        if(distance <= 100) {
          effects.capture_closing = true;
          object.flags |= 4u;
          if(object.collision_group == 1) {
            effects.capture_pending = effects.capture_closing = false;
            if(distance <= 32) effects.rescued = true;
          }
        }
        effects.approach_target = maths::world_position{object.position};
        return;
      }
    }
  }
  object.flags &= static_cast<uint16_t>(~6u);
  follow_vehicle(object, tier);
}

} // namespace corncob::game

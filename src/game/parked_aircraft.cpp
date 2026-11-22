#include "parked_aircraft.h"
#include <cstdlib>
#include "maths/integer.h"
#include "game/scenery.h"

namespace corncob::game {

parking_reset tower_parking_reset(maths::world_position const &tower, maths::world_position const &observer) noexcept {
  /// HOMEDETECT keeps parked poses only within 4,200 scaled units; its separate 5,200 limit controls home/radar status
  uint16_t constexpr near_tower{4200};
  maths::fixed_vector relative{0, 0, 0};
  for(unsigned int axis{0}; axis != 3; ++axis) {
    int32_t const delta{maths::dword(static_cast<int64_t>(tower[axis]) - observer[axis])};
    if(std::abs(static_cast<int64_t>(delta)) >= 524'288) return parking_reset::enable;
    relative[axis] = maths::negate(maths::word(delta >> 6));
  }
  auto const distance{maths::magnitude(relative)};
  return distance <= near_tower ? parking_reset::disable : parking_reset::enable;
}

bool advance_parked_aircraft(world::placed_object &object, maths::world_position const &base, maths::world_position const &observer,
                             aircraft_parking &parking, bool const on_foot, bool const exiting) noexcept {
  /// OBJ5DATIM repairs eligible home landings, arranges unclaimed planes and requests the original boarding handoff
  uint16_t constexpr permanently_crashed{0x100};
  uint16_t constexpr experimental{0x2000};
  uint16_t constexpr landing_bits{6};
  int32_t constexpr parking_spacing{7200};
  int32_t constexpr departure_offset{8000};
  if((object.flags & permanently_crashed) != 0) object.pose = world::model_pose::damaged;
  object.timer = 32'000;
  if(!exiting) {
    if(parking.resetting) {
      object.aircraft_parking_flags = 0;
    } else if((object.aircraft_parking_flags & aircraft_parking::keep_pose) == 0 && (object.flags & (permanently_crashed | experimental)) == 0
      && ((object.flags & landing_bits) == 0 || (object.flags & landing_bits) == landing_bits)) {
      object.flags &= static_cast<uint16_t>(~(permanently_crashed | landing_bits));
      object.position.z = base.z;
      object.attitude = parking.base_attitude;
      if((object.aircraft_parking_flags & aircraft_parking::positioned) == 0) {
        object.position = maths::world_position{base};
        object.position.x = maths::dword(static_cast<int64_t>(base.x) - static_cast<uint32_t>(parking.placed_count) * parking_spacing);
        object.attitude.heading = static_cast<maths::angle>(-8000);
        ++parking.placed_count;
        object.aircraft_parking_flags |= aircraft_parking::positioned;
      } else if(on_foot && parking.new_arrival && interaction_range(object.position, observer)) {
        parking.new_arrival = false;
        object.position.y = maths::dword(static_cast<int64_t>(object.position.y) - departure_offset);
        object.attitude.heading = 0;
        return true;
      }
    }
  }
  if(on_foot && !exiting) {
    auto const distance{interaction_range(object.position, observer)};
    if(distance && *distance <= contact_distance) return true;
  }
  if((object.flags & experimental) != 0) object.pose = world::model_pose::experimental_aircraft;
  return false;
}

} // namespace corncob::game

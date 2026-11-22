#include "airfields.h"
#include <algorithm>
#include <cstdlib>
#include <limits>

namespace corncob::world {

bool multiple_airfields(theatre const &world) noexcept {
  /// INT merges every tile into RDU when a universe has only one control tower.
  return std::ranges::count_if(world.objects, [](auto const &object){return object.type == object_type::control_tower && object.has(object_flag::present);}) > 1;
}
unsigned int airfield_for(maths::world_position const &position) noexcept {
  /// Original INT table DS:52d0 divides tile X into 3/2/3 columns and tile Y into 3/2/3 rows.
  auto band{[](int32_t const coordinate){
    int const tile{std::clamp(static_cast<int>((static_cast<int64_t>(coordinate) - theatre::origin) >> 21), 0, 7)};
    return tile < 3 ? 0u : tile < 5 ? 1u : 2u;
  }};
  return band(position.x) * 3 + band(position.y);
}
unsigned int nearest_airfield(theatre const &world, maths::world_position const &position) noexcept {
  if(!multiple_airfields(world)) return central_airfield;
  uint64_t distance{std::numeric_limits<uint64_t>::max()};
  unsigned int result{central_airfield};
  for(auto const &tower : world.objects) {
    if(tower.type != object_type::control_tower || !tower.has(object_flag::present)) continue;
    int64_t const dx{(static_cast<int64_t>(position.x) - tower.position.x) / 256};
    int64_t const dy{(static_cast<int64_t>(position.y) - tower.position.y) / 256};
    uint64_t const square{static_cast<uint64_t>(dx * dx + dy * dy)};
    if(square < distance) {
      distance = square;
      result = airfield_for(tower.position);
    }
  }
  return result;
}
bool belongs_to_airfield(theatre const &world, placed_object const &object, unsigned int const airfield) noexcept {
  return !multiple_airfields(world) || airfield_for(object.position) == airfield;
}
maths::world_position airfield_start(theatre const &world, unsigned int const airfield) {
  /// The original runway start is 128071 cm south and 16000 cm east of its tower, at the normal pilot/aircraft height.
  for(auto const &tower : world.objects) {
    if(tower.type == object_type::control_tower && tower.has(object_flag::present) && airfield_for(tower.position) == airfield) {
      return {tower.position.x - 128071, tower.position.y - 16000, 720};
    }
  }
  return maths::world_position{world.start};
}
void select_airfield(theatre &world, unsigned int const airfield) {
  if(multiple_airfields(world)) world.start = airfield_start(world, airfield);
}

} // namespace corncob::world

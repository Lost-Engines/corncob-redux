#include "slalom.h"

namespace corncob::game {

bool slalom_course::advance(maths::world_position const &position, bool const on_ground, bool const at_home) noexcept {
  /// DOSLAL compares signed high-word X sectors and alternating sides; skipped markers and away-landings reset all indicators.
  int32_t const previous{previous_x >> 17};
  int32_t const current{position.x >> 17};
  previous_x = position.x;
  if(on_ground && !at_home) {
    passed = 0;
    return false;
  }
  if(position.x < 0 || current <= previous || current >= static_cast<int32_t>(marker_count)) return false;
  int32_t const sideways{(position.y >> 16) - 2};
  bool const correct_side{(passed & 1u) != 0 ? sideways <= 0 : sideways >= 0};
  if(current != static_cast<int32_t>(passed) || !correct_side) {
    passed = 0;
    return false;
  }
  if(passed != marker_count) ++passed;
  if(passed != marker_count || awarded) return false;
  awarded = true;
  return true;
}

} // namespace corncob::game

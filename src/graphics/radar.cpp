#include "radar.h"
#include <algorithm>
#include <cstdlib>

namespace corncob::graphics {

screen_point radar_position(uint16_t const distance, maths::angle const bearing) noexcept {
  /// RADARDOT uses the high product word with independently stored horizontal/vertical radar diameters
  int const horizontal{distance * 120 / 256};
  int const vertical{distance * 76 / 256};
  return {
    561 + ((static_cast<int32_t>(maths::negate(maths::sine(bearing))) * horizontal) >> 16),
    295 + ((static_cast<int32_t>(maths::negate(maths::cosine(bearing))) * vertical) >> 16),
  };
}
std::optional<screen_point> radar_position(maths::world_position const &target, maths::world_position const &aircraft, maths::angle const heading) noexcept {
  /// RADARPROCESS uses horizontal distance relative to the aircraft, including after ejection; square-root compression fits distant contacts on the small display
  auto const delta{vec3<int64_t>{target} - vec3<int64_t>{aircraft}};
  if(std::max({std::abs(delta.x), std::abs(delta.y), std::abs(delta.z)}) >= 524'288) return std::nullopt;
  maths::fixed_vector const horizontal{maths::word(static_cast<int32_t>(delta.x) >> 4), maths::word(static_cast<int32_t>(delta.y) >> 4), 0};
  auto const bearing{static_cast<maths::angle>(maths::arctangent(horizontal.x, horizontal.y) - heading)};
  auto const distance{maths::square_root(maths::magnitude(horizontal))};
  return radar_position(distance, bearing);
}
void radar_dot(indexed_framebuffer &target, random_source &random, screen_point const &position, uint8_t const colour) {
  /// Small contacts share the authored three-pixel DRAWDisc mask and original radar clipping rectangle
  raster{target, radar_bounds, random}.disc(position, 3, colour);
}

} // namespace corncob::graphics

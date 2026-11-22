#include "homing.h"
#include <algorithm>
#include <cstdlib>

namespace corncob::graphics {

void homing_pointer(indexed_framebuffer &target, random_source &random, maths::world_position const &home,
                    maths::world_position const &observer, maths::angle const heading, int const view_bottom) {
  /// The original small black chevron slides along the grey viewport border and disappears very close to the airbase
  int16_t const x{maths::word((home.x - observer.x) >> 12)};
  int16_t const y{maths::word((home.y - observer.y) >> 12)};
  if(std::abs(static_cast<int>(x)) + std::abs(static_cast<int>(y)) <= 10) return;
  auto const bearing{maths::word(maths::arctangent(x, y) - heading)};
  int const half_width{static_cast<int>(target.width) / 2 - 10};
  int const position{half_width + std::clamp(-static_cast<int>(bearing >> 1), -4000, 4000) * half_width / 4000};
  raster draw{target, {0, 0, static_cast<int>(target.width), static_cast<int>(target.height)}, random};
  draw.line({position, view_bottom + 8}, {position + 2, view_bottom + 2}, 0);
  draw.line({position + 4, view_bottom + 8}, {position + 2, view_bottom + 2}, 0);
}

} // namespace corncob::graphics

#pragma once

#include <optional>
#include "maths/fixed.h"
#include "graphics/raster.h"

namespace corncob::graphics {

inline clip_rectangle constexpr radar_bounds{
  .left{496},
  .top{259},
  .right{615},
  .bottom{327},
};

screen_point radar_position(uint16_t distance, maths::angle bearing) noexcept;
std::optional<screen_point> radar_position(maths::world_position const &target, maths::world_position const &aircraft, maths::angle heading) noexcept;
void radar_dot(indexed_framebuffer &target, random_source &random, screen_point const &position, uint8_t colour);

} // namespace corncob::graphics

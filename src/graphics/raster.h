#pragma once

#include <cstdint>
#include <span>
#include <vector>
#include "vectorstorm/vector/vector2.h"
#include "maths/random.h"
#include "render/framebuffer.h"
#include "render/video_mode.h"

namespace corncob::graphics {

using screen_point = vec2<int>;
using colour_index = uint8_t;

struct clip_rectangle {
  int left;
  int top;
  int right;
  int bottom;
};

bool clip_wire_line(screen_point &start, screen_point &end, clip_rectangle const &bounds) noexcept;

struct indexed_framebuffer {
  unsigned int width{render::screen_layout::width};
  unsigned int height{render::screen_layout::height};
  bool monochrome{false};
  std::vector<colour_index> pixels = std::vector<colour_index>(render::screen_layout::pixel_count);
};

using random_source = maths::random_source;

enum class pixel_operation {
  replace,
  high_plane,
};

class raster {
private:
  indexed_framebuffer &target;
  clip_rectangle bounds;
  random_source &random;
  pixel_operation operation;

  bool clip_line(screen_point &start, screen_point &end) const noexcept;
  void raw_pixel(int x, int y, colour_index colour, bool patterned = true) noexcept;
  void span(int y, int left, int right, colour_index colour, uint8_t mask = 0xff) noexcept;

public:
  raster(indexed_framebuffer &target, clip_rectangle bounds, random_source &random, pixel_operation operation = pixel_operation::replace);
  void point(screen_point const &position, colour_index colour) noexcept;
  void line(screen_point start, screen_point end, colour_index colour) noexcept;
  void wire(screen_point start, screen_point end, colour_index colour) noexcept;
  void polygon(std::span<screen_point const> vertices, colour_index colour) noexcept;
  void disc(screen_point const &centre, int diameter, colour_index colour) noexcept;
};

} // namespace corncob::graphics

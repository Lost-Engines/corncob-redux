#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace corncob::render {

struct screen_layout {
  static unsigned int constexpr width{640};
  static unsigned int constexpr height{350};
  static unsigned int constexpr pixel_count{width * height};
  static unsigned int constexpr mask_stride{width / 8};
  static unsigned int constexpr mask_bytes{mask_stride * height};
  static int constexpr display_width{640};
  static int constexpr crt_height{480};
};

struct rgba_pixel {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t alpha{255};
};
static_assert(sizeof(rgba_pixel) == 4);

struct framebuffer {
  unsigned int width{screen_layout::width};
  unsigned int height{screen_layout::height};
  std::vector<rgba_pixel> pixels{screen_layout::pixel_count};
};

enum class display_aspect {
  square_pixels,
  crt
};

struct viewport {
  int x;
  int y;
  int width;
  int height;
};

viewport fit_viewport(int width, int height, int source_width = screen_layout::width, int source_height = screen_layout::height,
                      display_aspect aspect = display_aspect::square_pixels) noexcept;

} // namespace corncob::render

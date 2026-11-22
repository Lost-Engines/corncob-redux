#include "framebuffer.h"
#include <algorithm>
#include <cmath>

namespace corncob::render {

viewport fit_viewport(int const width, int const height, int const source_width, int const source_height, display_aspect const aspect) noexcept {
  /// Enlarge the actual video mode by whole pixels; CRT correction deliberately changes their aspect ratio.
  if(width <= 0 || height <= 0 || source_width <= 0 || source_height <= 0) {
    return {
      .x{0},
      .y{0},
      .width{0},
      .height{0},
    };
  }
  int const image_width{source_width};
  int const image_height{aspect == display_aspect::crt ? source_width * 3 / 4 : source_height};
  double const available{std::min(width / static_cast<double>(image_width), height / static_cast<double>(image_height))};
  double const scale{available >= 1.0 ? std::floor(available) : available};
  int const fitted_width{std::max(1, static_cast<int>(image_width * scale))};
  int const fitted_height{std::max(1, static_cast<int>(image_height * scale))};
  return {
    .x{(width - fitted_width) / 2},
    .y{(height - fitted_height) / 2},
    .width{fitted_width},
    .height{fitted_height},
  };
}

} // namespace corncob::render

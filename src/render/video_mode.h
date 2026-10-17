#pragma once

#include <array>
#include <cstdint>

namespace corncob::render {

enum class video_mode {
  ega,
  ega_low,
  hercules,
};
struct resolution_limits {
  static unsigned int constexpr minimum_width{160}, minimum_height{100};
  static unsigned int constexpr maximum_width{7680}, maximum_height{4320};
};
struct video_size {
  unsigned int width;
  unsigned int height;
};
constexpr video_size dimensions(video_mode const mode) noexcept {
  /// Original 3DVID.INC: BIOS 10h, BIOS 0Dh and the software-programmed Hercules 81h branch.
  switch(mode) {
  case video_mode::ega_low: return {
    .width{320},
    .height{200},
  };
  case video_mode::hercules: return {
    .width{720},
    .height{348},
  };
  default: return {
    .width{640},
    .height{350},
  };
  }
}
constexpr uint8_t hercules_colour(uint8_t const colour, int const x, bool const patterned = true) noexcept {
  /// F3DVEC's BKCLRS supplies horizontal fill masks; HGCLINE treats each nonzero pattern as a solid white line.
  std::array<uint8_t, 16> constexpr patterns{0, 0, 0x44, 0xff, 0, 0, 0, 0x55, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  uint8_t const pattern{patterns[colour & 15u]};
  return (patterned ? (pattern & (0x80u >> (x & 7))) != 0 : pattern != 0) ? 15 : 0;
}

} // namespace corncob::render

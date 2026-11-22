#include "palette.h"

namespace corncob::render {

void fade_palette(std::array<rgba_pixel, 16> &colours, uint8_t const intensity) noexcept {
  /// SETCOLORS multiplies original six-bit DAC values and takes the high product byte; even 255 is not an identity
  for(auto &colour : colours) {
    for(auto *component : {&colour.red, &colour.green, &colour.blue}) {
      unsigned int const value{(static_cast<unsigned int>(*component >> 2) * intensity) >> 8};
      *component = static_cast<uint8_t>((value << 2) | (value >> 4));
    }
  }
}

} // namespace corncob::render

#include "model_animation.h"

namespace corncob::resources {

namespace {

int16_t wall_size(maths::angle const phase, int16_t const amplitude, int16_t const maximum) noexcept {
  /// CHANG EWALL keeps a nonzero half-size and clamps signed overflow at the authored maximum
  int16_t const size{maths::word(maths::multiply(maths::cosine(phase), amplitude) + amplitude)};
  return size == 0 ? 1 : size < 0 ? maximum : size;
}

} // anonymous namespace

model_animation model_animation::at(unsigned int const frame) noexcept {
  /// Three independent native phases drive wire geometry; the second also gives the saucer its opposing disc diameters
  int16_t const disc_delta{maths::multiply(maths::cosine(static_cast<maths::angle>(frame * 3111u)), 4000 / 12)};
  return {
    .wide_wall{wall_size(static_cast<maths::angle>(frame << 12), 8191, 16'382)},
    .small_wall{wall_size(static_cast<maths::angle>(frame * 3111u), 4095, 8191)},
    .square_wall{wall_size(static_cast<maths::angle>(frame * 5333u), 8191, 16'383)},
    .upper_saucer{static_cast<uint16_t>(999 + disc_delta)},
    .lower_saucer{static_cast<uint16_t>(999 - disc_delta)},
  };
}
int16_t model_animation::value(model_pulse const pulse) const noexcept {
  /// Typed bindings distinguish signed vertex magnitudes from the two positive disc diameters
  switch(pulse) {
  case model_pulse::wide_wall:
    return wide_wall;
  case model_pulse::small_wall:
    return small_wall;
  case model_pulse::square_wall:
    return square_wall;
  case model_pulse::upper_saucer:
    return maths::word(upper_saucer);
  case model_pulse::lower_saucer:
    return maths::word(lower_saucer);
  }
  return 0;
}

} // namespace corncob::resources

#include "particle_animation.h"
#include "resources/binary_reader.h"

namespace corncob::resources {

namespace {

std::array<uint16_t, 3> constexpr live_addresses{0x36fb, 0x35b7, 0x3e54};
std::array<uint16_t, 3> constexpr base_addresses{0x379d, 0x3659, 0x3e74};
std::array<unsigned int, 3> constexpr disc_counts{16, 16, 3};

} // anonymous namespace

particle_animation::particle_animation(std::span<uint8_t const> const source) {
  /// Retain both the authored initial shapes and the separate immutable templates used by the native random animation
  binary_reader const input{source};
  unsigned int index{0};
  for(unsigned int bank{0}; bank != disc_counts.size(); ++bank) {
    for(unsigned int disc{0}; disc != disc_counts[bank]; ++disc) {
      unsigned int const live{live_addresses[bank] + 2 + disc * 10};
      unsigned int const base{base_addresses[bank] + 2 + disc * 10};
      current[index] = {
        .position{input.vector(live)},
        .diameter{input.word(live + 6)},
      };
      bases[index++] = {
        .position{input.vector(base)},
        .diameter{input.word(base + 6)},
      };
    }
  }
  headquarters_colour = input.byte(0x31b6);
}
std::optional<unsigned int> particle_animation::binding(uint16_t const address) noexcept {
  /// Original disc-cloud records contain four animated words followed by an unchanged colour reference
  unsigned int index{0};
  for(unsigned int bank{0}; bank != disc_counts.size(); ++bank) {
    int const offset{address - live_addresses[bank] - 2};
    if(offset >= 0 && offset % 10 == 0 && static_cast<unsigned int>(offset / 10) < disc_counts[bank]) return index + static_cast<unsigned int>(offset / 10);
    index += disc_counts[bank];
  }
  return std::nullopt;
}
void particle_animation::advance(maths::random_source &random) noexcept {
  /// Each native frame draws one shared colour and 140 independent disc-coordinate/diameter offsets for the following frame
  headquarters_colour = static_cast<uint8_t>(random.below(15));
  unsigned int index{0};
  std::array<uint16_t, 3> constexpr first_ranges{300, 60, 500};
  std::array<uint16_t, 3> constexpr range_steps{30, 7, 62};
  std::array<uint16_t, 3> constexpr biases{0, 120, 1000};
  for(unsigned int bank{0}; bank != disc_counts.size(); ++bank) {
    for(unsigned int disc{0}; disc != disc_counts[bank]; ++disc) {
      auto const range{static_cast<uint16_t>(first_ranges[bank] + disc * range_steps[bank])};
      for(unsigned int axis{0}; axis != 3; ++axis) current[index].position[axis] = maths::word(bases[index].position[axis] + random.below(range) + biases[bank]);
      current[index].diameter = static_cast<uint16_t>(bases[index].diameter + random.below(range) + biases[bank]);
      ++index;
    }
  }
}
std::span<particle_disc const> particle_animation::discs() const noexcept {
  /// Return the current large-cloud, small-cloud and flotsam shapes in original bank order
  return current;
}
uint8_t particle_animation::colour() const noexcept {
  /// Quality rows and headquarters primitives share the previous frame's single random colour
  return headquarters_colour;
}

} // namespace corncob::resources

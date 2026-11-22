#pragma once

#include <cstdint>

namespace corncob::maths {

struct random_word {
  uint16_t value;
  bool carry;
};

class random_source {
private:
  uint32_t state;

public:
  explicit random_source(uint32_t const seed = 2'949'243u) noexcept : state{seed} {
    /// Start the original wrapping multiplicative random stream
  }
  uint16_t next() noexcept {
    /// RAND_W exposes the high word of the updated dword state
    state *= 0x278dde6du;
    return static_cast<uint16_t>(state >> 16);
  }
  random_word next_with_carry() noexcept {
    /// RAND_W leaves the final high-word addition carry live; the bee handler accidentally consumes it as a contact flag
    uint32_t constexpr multiplier{0x278dde6du};
    uint32_t const low{state & 0xffffu};
    uint32_t const high{state >> 16};
    uint32_t const partial{((low * (multiplier >> 16)) + ((low * (multiplier & 0xffffu)) >> 16)) & 0xffffu};
    bool const carry{partial + ((high * (multiplier & 0xffffu)) & 0xffffu) > 0xffffu};
    return {
      .value{next()},
      .carry{carry},
    };
  }
  uint16_t below(uint16_t const limit) noexcept {
    /// RANDR_W keeps the remainder, retaining the unrestricted result for a zero range
    auto const value{next()};
    return limit == 0 ? value : static_cast<uint16_t>(value % limit);
  }
  uint32_t seed() const noexcept {
    /// Expose the continuation state for saves and native comparisons
    return state;
  }
};

} // namespace corncob::maths

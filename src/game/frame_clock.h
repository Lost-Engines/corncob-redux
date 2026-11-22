#pragma once

#include <cstdint>

namespace corncob::game {

constexpr uint16_t advance_frame(uint16_t const current) noexcept {
  /// The native word counter restarts at 128 before reaching 16,384, retaining its startup-only phase
  uint16_t constexpr cycle_end{16'384};
  uint16_t constexpr cycle_restart{128};
  auto const next{static_cast<uint16_t>(current + 1u)};
  return next >= cycle_end ? cycle_restart : next;
}

} // namespace corncob::game

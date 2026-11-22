#include "sortie_clock.h"

namespace corncob::game {

sortie_clock::sortie_clock(uint16_t const recorded_seconds) noexcept :
  recorded_seconds{recorded_seconds} {
  /// A native flight result owns a wrapping word of recorded elapsed seconds
}
void sortie_clock::advance(uint32_t const ticks) noexcept {
  /// Count the original PIT time independently of accelerated physics; map inspection retains its elapsed clock
  pending_ticks += ticks;
}
uint16_t sortie_clock::elapsed() const noexcept {
  /// ADDTOETIME uses only minutes/seconds from SAS TMR_READ; its hour component is deliberately ignored
  uint64_t constexpr pit_frequency{1'193'182};
  uint64_t constexpr pit_divisor{1024};
  uint64_t constexpr seconds_per_hour{3600};
  uint64_t const seconds{static_cast<uint64_t>(pending_ticks) * pit_divisor / pit_frequency};
  return static_cast<uint16_t>(recorded_seconds + seconds % seconds_per_hour);
}
void sortie_clock::suspend() noexcept {
  /// Pause/help/radio/tower handoffs commit complete seconds and discard the original partial second before restarting the segment
  recorded_seconds = elapsed();
  pending_ticks = 0;
}

} // namespace corncob::game

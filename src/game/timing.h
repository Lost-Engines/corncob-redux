#pragma once

#include <cstdint>
#include <algorithm>
#include <cmath>
#include <optional>

namespace corncob::game::timing {

inline int16_t constexpr physics_units_per_step{5};
inline uint16_t constexpr pit_ticks_per_step{40};
inline double constexpr pit_frequency{1193182.0 / 1024.0};
inline double constexpr seconds_per_step{pit_ticks_per_step / pit_frequency};
inline unsigned int constexpr fast_forward_multiplier{10};

inline double constexpr maximum_step{1.0 / 60.0};
inline double constexpr maximum_frame_time{0.1};

// The platform supplies monotonic timestamps; simulation time does not assume regular callbacks.
class frame_clock {
  std::optional<double> previous;
public:
  double advance(double timestamp, bool running = true, bool accelerated = false) noexcept {
    if(!std::isfinite(timestamp)) { reset(); return 0; }
    if(!running) { reset(); return 0; }
    auto const last{previous}; previous = timestamp;
    if(!last || timestamp < *last) return 0;
    return std::min(timestamp - *last, maximum_frame_time) * (accelerated ? fast_forward_multiplier : 1);
  }
  void reset() noexcept { previous.reset(); }
};

// Kept for native reference/check callers that explicitly request original five-unit passes.
class simulation_clock {
private:
  double accumulated_seconds{0.0};

public:
  void add_time(double const elapsed, bool const fast_forward) noexcept {
    // acceleration schedules more complete updates; physics and enemy timers keep the same step
    accumulated_seconds += elapsed * (fast_forward ? fast_forward_multiplier : 1);
  }
  bool take_step() noexcept {
    if(accumulated_seconds < seconds_per_step) return false;
    accumulated_seconds -= seconds_per_step;
    return true;
  }
  void reset() noexcept {
    accumulated_seconds = 0.0;
  }
};

} // namespace corncob::game::timing

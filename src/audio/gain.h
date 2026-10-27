#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

namespace corncob::audio {

// Main-thread requests cross atomically; only the PCM callback advances the ramp.
class gain_control {
  std::atomic<float> requested{1.0f};
  float current{1.0f};
  bool started{false};
public:
  void set(float const percent) noexcept {
    if(std::isfinite(percent)) requested.store(std::clamp(percent,0.0f,100.0f)/100, std::memory_order_relaxed);
  }
  float begin() noexcept {
    float const target{requested.load(std::memory_order_relaxed)};
    if(!started) { current = target; started = true; }
    return target;
  }
  void settle(float const target) noexcept { current = target; }
  float next(float const target) noexcept {
    float constexpr step{1.0f/(48000*0.005f)};
    current += std::clamp(target-current,-step,step);
    return current;
  }
};

} // namespace corncob::audio

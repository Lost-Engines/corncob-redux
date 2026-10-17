#pragma once

#include <cstdint>

namespace corncob::game::timing {

inline int16_t constexpr physics_units_per_step{5};
inline uint16_t constexpr pit_ticks_per_step{40};
inline double constexpr pit_frequency{1193182.0 / 1024.0};
inline double constexpr seconds_per_step{pit_ticks_per_step / pit_frequency};

} // namespace corncob::game::timing

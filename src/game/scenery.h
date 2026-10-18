#pragma once

#include <cstdint>
#include <optional>
#include "world/world.h"

namespace corncob::game {

inline uint16_t constexpr contact_distance{2000};
inline uint16_t constexpr reentry_departure_distance{4000};
inline int16_t constexpr aircraft_exit_aft{1200}, experimental_exit_aft{2000}, aircraft_exit_side{1800};

enum class scenery_event {
  none,
  contact,
  tower,
};

std::optional<uint16_t> interaction_range(maths::world_position const &position, maths::world_position const &observer) noexcept;
scenery_event advance_scenery(world::placed_object &object, maths::world_position const &observer, bool on_foot, uint32_t fast_ticks) noexcept;

} // namespace corncob::game

#pragma once

#include <stdexcept>
#include <string>
#include "resources/binary_reader.h"
#include "world/world.h"

namespace corncob::tests {

inline corncob::world::placed_object read_actor(corncob::resources::binary_reader const &data, unsigned int const offset) {
  /// Decode semantic native fields while leaving DOS pointers and painter bookkeeping inside the fixture
  return {
    .position{data.signed_dword(offset), data.signed_dword(offset + 4), data.signed_dword(offset + 8)},
    .attitude{
      .heading{data.word(offset + 12)},
      .pitch{data.word(offset + 14)},
      .roll{data.word(offset + 16)},
    },
    .velocity{data.vector(offset + 46)},
    .type{static_cast<corncob::world::object_type>(data.word(offset + 72))},
    .flags{data.word(offset + 24)},
    .timer{data.word(offset + 52)},
    .health{data.byte(offset + 27)},
    .collision_group{data.byte(offset + 19)},
    .motion{
      .spin{data.vector(offset + 54)},
      .dispersion{data.signed_word(offset + 60)},
      .drag_divisor{data.signed_word(offset + 62)},
      .gravity{data.signed_word(offset + 64)},
    },
    .collision_extents{data.vector(offset + 66)},
  };
}
inline void compare_actor(corncob::world::placed_object const &actual, corncob::world::placed_object const &expected, unsigned int const index, char const *const name) {
  /// Check full constructed actor state rather than only whether a new object appeared
  auto check{[&](bool const value, char const *const field){if(!value) throw std::runtime_error{"native actor case " + std::to_string(index) + " " + name + " " + field};}};
  check(actual.position == expected.position, "position");
  check(actual.attitude.heading == expected.attitude.heading && actual.attitude.pitch == expected.attitude.pitch && actual.attitude.roll == expected.attitude.roll, "attitude");
  check(actual.velocity == expected.velocity, "velocity");
  check(actual.flags == expected.flags, "flags");
  check(actual.timer == expected.timer, "timer");
  check(actual.health == expected.health && actual.collision_group == expected.collision_group, "health/group");
  check(actual.motion.spin == expected.motion.spin && actual.motion.dispersion == expected.motion.dispersion
    && actual.motion.drag_divisor == expected.motion.drag_divisor && actual.motion.gravity == expected.motion.gravity, "motion");
  check(actual.collision_extents == expected.collision_extents && actual.type == expected.type, "type/extents");
}

} // namespace corncob::tests

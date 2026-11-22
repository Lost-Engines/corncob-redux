#pragma once

#include "maths/random.h"
#include "flight/aircraft.h"
#include "world/world.h"

namespace corncob::world {

bool advance_motion(placed_object &object, bool rotates, maths::random_source &random, flight::time_step step);
bool boxes_overlap(maths::world_position const &a, maths::fixed_vector const &a_extents,
                   maths::world_position const &b, maths::fixed_vector const &b_extents) noexcept;

} // namespace corncob::world

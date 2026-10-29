#pragma once

#include <array>
#include <span>
#include <vector>
#include "maths/fixed.h"
#include "maths/random.h"

namespace corncob::graphics {

void regenerate_ground_point(maths::world_position &point, maths::world_position const &observer, maths::random_source &random, double range_multiplier = 1.0) noexcept;
maths::world_position generate_star_point(maths::random_source &random) noexcept;

class ground_field {
private:
  std::vector<maths::world_position> ground = std::vector<maths::world_position>(80);
  std::array<maths::world_position, 80> sky{};
  bool ready{false};
  bool tracking{false};
  double range_multiplier{1.0};
  double range_exponent{4.0};

  double point_range(unsigned int index) const noexcept;

public:
  void density(unsigned int count);
  void range(double multiplier);
  void initialise(maths::world_position const &observer, maths::random_source &random) noexcept;
  void update(maths::world_position const &observer, maths::random_source &random) noexcept;
  std::span<maths::world_position> points() noexcept;
  std::span<maths::world_position const> stars() const noexcept;
};

} // namespace corncob::graphics

#include "ground_detail.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include "maths/integer.h"

namespace corncob::graphics {

namespace {

maths::world_position distant_point(uint16_t const width, int32_t const height, maths::random_source &random) noexcept {
  /// GRAND/SGRNDPT draw each signed high word before its independent low word
  auto coordinate{[width, &random]{
    int32_t const high{random.below(width) - width / 2};
    return high * 65'536 + random.next();
  }};
  int32_t const x{coordinate()}, y{coordinate()};
  return {x, y, height};
}

} // anonymous namespace

void regenerate_ground_point(maths::world_position &point, maths::world_position const &observer, maths::random_source &random, double const range_multiplier) noexcept {
  /// GRNDPT leaves underground points untouched; DRANDR keeps the original wrapping range and signed half-range subtraction
  if(observer.z < 0) return;
  uint32_t const native_range{static_cast<uint32_t>(observer.z) * 64u};
  uint32_t const range{range_multiplier == 1.0 ? native_range
    : static_cast<uint32_t>(std::clamp(native_range * range_multiplier, 0.0, static_cast<double>(std::numeric_limits<int32_t>::max())))};
  auto coordinate{[range, &random](int32_t const centre){
    int64_t const product{static_cast<int64_t>(static_cast<uint64_t>(range) * random.next() >> 16)};
    return maths::dword(static_cast<int64_t>(centre) + product - (maths::dword(range) >> 1));
  }};
  point = maths::world_position{coordinate(observer.x), coordinate(observer.y), 0};
}
maths::world_position generate_star_point(maths::random_source &random) noexcept {
  /// The original stars are world points at a fixed height, rather than a static bitmap or a pregenerated constant bank
  return distant_point(16'384, 0x0800'0000, random);
}
void ground_field::density(unsigned int const count) {
  if(ground.size() == count) return;
  ground.resize(count);
  ready = tracking = false;
}
void ground_field::range(double const multiplier) {
  if(range_multiplier == multiplier) return;
  range_multiplier = multiplier;
  range_exponent = std::log2(multiplier / std::min(multiplier, 0.0625));
  ready = tracking = false;
}
double ground_field::point_range(unsigned int const index) const noexcept {
  // Include the ground beneath the lower viewport, then spread points across distance scales.
  // Stagger their edges to avoid a common cutoff at the horizon.
  double const fraction{std::min(1.0, index / static_cast<double>(std::max<std::size_t>(2, ground.size()) - 1))};
  return std::min(range_multiplier, 0.0625) * std::exp2(range_exponent * fraction);
}
void ground_field::initialise(maths::world_position const &observer, maths::random_source &random) noexcept {
  /// Initialise all three eighty-point banks, including the unused far bank and final records beyond the drawing loop
  if(ready) return;
  for(unsigned int i{0}; i != ground.size(); ++i) {
    maths::world_position point{0, 0, 0};
    regenerate_ground_point(point, observer, random, range_multiplier);
    if(i + 1 < ground.size()) ground[i + 1] = maths::world_position{point};
  }
  for(unsigned int i{0}; i != ground.size(); ++i) (void)distant_point(6, 0, random);
  for(unsigned int i{0}; i != sky.size(); ++i) {
    auto const point{generate_star_point(random)};
    if(i + 1 < sky.size()) sky[i + 1] = maths::world_position{point};
  }
  ready = true;
}
void ground_field::update(maths::world_position const &observer, maths::random_source &random) noexcept {
  if(observer.z < 0) return;
  maths::world_position const centre{observer.x, observer.y, std::max(observer.z, 100)};
  initialise(centre, random);
  if(!tracking) {
    for(unsigned int i{0}; i < ground.size(); ++i) regenerate_ground_point(ground[i], centre, random, point_range(i));
    tracking = true;
  }
  for(unsigned int i{0}; i < ground.size(); ++i) {
    int64_t const width{static_cast<int64_t>(std::clamp(centre.z * 64.0 * point_range(i), 2.0, static_cast<double>(std::numeric_limits<int32_t>::max())))};
    for(unsigned int axis{0}; axis < 2; ++axis) {
      int64_t const relative{maths::dword(static_cast<int64_t>(ground[i][axis]) - centre[axis])};
      int64_t const wrapped{((relative + width / 2) % width + width) % width - width / 2};
      ground[i][axis] = maths::dword(static_cast<int64_t>(centre[axis]) + wrapped);
    }
  }
}
std::span<maths::world_position> ground_field::points() noexcept {
  /// Redux tracks these world points by position, independently of visibility.
  return ground;
}
std::span<maths::world_position const> ground_field::stars() const noexcept {
  /// The star bank retains its anchor and first seventy-nine generated records throughout the sortie
  return sky;
}

} // namespace corncob::graphics

#include "portal.h"

namespace corncob::game {

void transfer_portal(maths::world_position &position, maths::world_position const &start, portal_transfer const transfer, maths::random_source &random) noexcept {
  /// DOXP draws both signed tile words before both fractional words; a transfer always arrives at the original fixed altitude
  int32_t constexpr arrival_altitude{3 * 65'536 + 32'768};
  uint16_t constexpr coordinate_span{256};
  int32_t constexpr coordinate_bias{128};
  if(transfer == portal_transfer::none) return;
  if(transfer == portal_transfer::random) {
    int32_t const high_x{static_cast<int32_t>(random.below(coordinate_span)) - coordinate_bias};
    int32_t const high_y{static_cast<int32_t>(random.below(coordinate_span)) - coordinate_bias};
    uint16_t const low_x{random.next()};
    uint16_t const low_y{random.next()};
    position.x = high_x * 65'536 + low_x;
    position.y = high_y * 65'536 + low_y;
  } else {
    position = maths::world_position{start};
  }
  position.z = arrival_altitude;
}

} // namespace corncob::game

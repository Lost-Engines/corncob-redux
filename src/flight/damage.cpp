#include "damage.h"
#include <algorithm>
#include <cstdlib>

namespace corncob::flight {

void apply_damage(damage_state &damage, aircraft_state &aircraft, unsigned int const events, maths::random_source &random, time_step const step) {
  /// DAMAGEPLANE assigns each event to engine, wing, stability or rudder; total damage is not a generic health bar
  if(damage.invincible || damage.total > 200) return;
  auto unit{[&](int16_t const value){return maths::word(static_cast<int32_t>(value) * step.normal / step.elapsed);}};
  for(unsigned int i{0}; i != events; ++i) {
    ++damage.total;
    switch(random.below(4)) {
    case 0:
      {
      ++damage.engine;
      auto const maximum{static_cast<uint16_t>(damage.maximum_throttle * 950u / 1000u)};
      if(maximum >= 20) {
        ++damage.indicator_hits[0];
        damage.maximum_throttle = maximum;
        aircraft.throttle = std::min(aircraft.throttle, maximum);
      }
    }
      break;
    case 1:
      if(std::abs(static_cast<int>(damage.wing)) <= 1600) {
        int16_t change{unit(40)};
        if((random.next() & 0x8000u) != 0) change = maths::negate(change);
        damage.wing = maths::word(damage.wing + change);
        ++damage.indicator_hits[1];
        damage.wing_loss = maths::word(damage.wing_loss + 3);
      }
      break;
    case 2:
      ++damage.indicator_hits[2];
      damage.stability = maths::word(damage.stability + unit(15));
      break;
    case 3:
      if(std::abs(static_cast<int>(damage.rudder)) <= 300) {
        int16_t change{unit(5)};
        int16_t const direction{maths::word(random.next())};
        if((damage.rudder == 0 ? direction : damage.rudder) < 0) change = maths::negate(change);
        ++damage.indicator_hits[3];
        damage.rudder = maths::word(damage.rudder + change);
      }
      break;
    }
  }
  damage.worst = std::max(damage.worst, damage.total);
}

} // namespace corncob::flight

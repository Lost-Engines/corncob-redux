#include "map_view.h"
#include <algorithm>
#include "maths/integer.h"

namespace corncob::game {

map_view::map_view(maths::world_position const &position) :
  origin{position},
  observer{
    .position{position.x, position.y, 210 * 65'536},
    .attitude{
      .pitch{16'384},
    },
    .cockpit{false},
    .rear_view{false},
    .ground_detail{false},
    .map_mode{true},
    .map_position{maths::world_position{position}},
  } {
  /// The original map starts directly above the retained pilot at 210 world high words, about sixty-five miles
}
void map_view::advance(flight::control_input const &stick, map_input const &input, double const ratio) {
  /// MAPKEY retains altitude while centring Home, pans by altitude, and uses Shift for free viewing angles
  if(input.home) {
    observer.position.x = origin.x;
    observer.position.y = origin.y;
    observer.attitude = {
      .pitch{16'384},
    };
  }
  if(ratio != 1.0) {
    if(input.rotate) {
      maths::turn(observer.attitude.heading, -stick.roll*ratio, fraction.angles.x);
      maths::turn(observer.attitude.pitch, -stick.pitch*ratio, fraction.angles.y);
      return;
    }
    vec3<double> movement{-stick.pitch*(observer.position.z/16.0)/1024.0*ratio,
                          -stick.roll*(observer.position.z/16.0)/1024.0*ratio, 0};
    if((input.higher && observer.position.z < 210*65536) || input.lower) {
      if(input.slow) movement.z = (input.lower ? -1000 : 1000)*ratio;
      else movement.z = (observer.position.z+640.0)*(std::pow(input.lower ? 15.0/16.0 : 17.0/16.0,ratio)-1);
    }
    maths::move(observer.position, movement, fraction.position);
    observer.position.z = std::clamp(observer.position.z, 0, 210*65536);
    return;
  }
  if(input.rotate) {
    observer.attitude.heading += static_cast<maths::angle>(-stick.roll);
    observer.attitude.pitch += static_cast<maths::angle>(-stick.pitch);
    return;
  }
  observer.position.x = maths::dword(static_cast<int64_t>(observer.position.x) - static_cast<int64_t>(stick.pitch) * (observer.position.z >> 4) / 1024);
  observer.position.y = maths::dword(static_cast<int64_t>(observer.position.y) - static_cast<int64_t>(stick.roll) * (observer.position.z >> 4) / 1024);
  if((input.higher && observer.position.z < 210 * 65'536) || input.lower) {
    int const amount{input.slow ? 1000 : (observer.position.z >> 4) + 40};
    observer.position.z = std::clamp(observer.position.z + (input.lower ? -amount : amount), 0, 210 * 65'536);
  }
}
graphics::camera map_view::camera() const {
  /// Map presentation owns its observer without relocating the pilot or running flight actors
  return observer;
}

} // namespace corncob::game

#include "joystick.h"
#include <algorithm>
#include <cstdlib>

namespace corncob::game {

int16_t joystick_axis(int16_t const displacement) noexcept {
  /// SPKE squares the calibrated axis after scaling by 0x205; JOYTD clips its signed output to half of 0x400
  unsigned int constexpr maximum_sample{32'767};
  unsigned int constexpr half_range{512};
  unsigned int const magnitude{std::min(static_cast<unsigned int>(std::abs(static_cast<int>(displacement))), maximum_sample)};
  unsigned int const scaled{magnitude * 0x205u / maximum_sample};
  int const result{static_cast<int>(std::min(scaled * scaled / half_range, half_range))};
  return static_cast<int16_t>(displacement < 0 ? -result : result);
}
void apply_joystick(input_state &input, joystick_sample const &sample, bool const keypad_look) noexcept {
  /// Keyboard deflection overrides both stick axes; head-view keys leave the physical joystick available for flight
  if(keypad_look || (input.stick.roll == 0 && input.stick.pitch == 0)) {
    input.stick.roll = joystick_axis(sample.axes.x);
    input.stick.pitch = joystick_axis(sample.axes.y);
    input.joystick_controlled = true;
  }
  // Keep keyboard weapon bindings stable when a joystick is connected; its buttons add independent requests.
  input.weapons.gun = input.weapons.gun || sample.gun;
  input.weapons.missile = input.weapons.missile || sample.missile;
}

} // namespace corncob::game

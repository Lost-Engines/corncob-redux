#include "joystick.h"
#include <algorithm>
#include <cmath>
#include <GLFW/glfw3.h>

namespace corncob::platform {

std::optional<game::joystick_sample> poll_joystick(int const requested) {
  /// Digital devices provide calibrated axes; retain the original response curve without analogue-port timing or recalibration
  if(requested < -1) return std::nullopt;
  for(int device{requested < 0 ? GLFW_JOYSTICK_1 : requested}; device <= (requested < 0 ? GLFW_JOYSTICK_LAST : requested); ++device) {
    if(!glfwJoystickPresent(device)) continue;
    int axis_count{0};
    int button_count{0};
    auto const *axes{glfwGetJoystickAxes(device, &axis_count)};
    auto const *buttons{glfwGetJoystickButtons(device, &button_count)};
    if(axes == nullptr || axis_count < 2) continue;
    auto axis{[](float const value){
      return std::isfinite(value) ? static_cast<int16_t>(std::lround(std::clamp(value, -1.0f, 1.0f) * 32'767.0f)) : int16_t{0};
    }};
    return game::joystick_sample{
      .axes{axis(axes[0]), axis(axes[1])},
      .gun{button_count >= 1 && buttons[0] == GLFW_PRESS},
      .missile{button_count >= 2 && buttons[1] == GLFW_PRESS},
    };
  }
  return std::nullopt;
}

} // namespace corncob::platform

#include "preferences.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include "platform/key_codes.h"
#include "platform/storage.h"

namespace corncob::game {

namespace {

namespace keys = platform::key;

control_binding binding(control_id const id, std::string name, control_context const context, int const key, int const alternate = keys::unbound, int const mouse = -1) {
  return {
    .id{id},
    .name{std::move(name)},
    .context{context},
    .key{key},
    .alternate{alternate},
    .mouse{mouse},
  };
}

} // anonymous namespace

std::vector<control_binding> default_bindings() {
  using enum control_id;
  using enum control_context;
  // GLFW's portable key codes: printable keys are ASCII; named keys occupy 256 and above.
  return {
    binding(roll_left, "Roll left", flight, keys::left, keys::keypad_4), binding(roll_right, "Roll right", flight, keys::right, keys::keypad_6),
    binding(pitch_up, "Pitch up", flight, keys::down, keys::keypad_2), binding(pitch_down, "Pitch down", flight, keys::up, keys::keypad_8),
    binding(rudder_left, "Rudder left", flight, 'Z', keys::keypad_7), binding(rudder_right, "Rudder right", flight, 'X', keys::keypad_9),
    binding(brakes, "Wheel brakes", flight, '.', keys::keypad_0), binding(throttle_up, "Throttle up", flight, '=', keys::keypad_add),
    binding(throttle_down, "Throttle down", flight, '-', keys::keypad_subtract),
    binding(forward, "Walk forward", walking, 'W'), binding(backward, "Walk backward", walking, 'S'),
    binding(strafe_left, "Strafe left", walking, 'A'), binding(strafe_right, "Strafe right", walking, 'D'),
    binding(jump, "Jump", walking, ' '), binding(drop_through, "Drop through platform", walking, keys::left_control),
    binding(gun, "Machine guns", flight, ' ', -1, 0), binding(pistol, "Pistol", walking, -1, -1, 0),
    binding(cannon, "Heavy cannon", flight, keys::left_shift), binding(missile, "Missiles", flight, 'C', -1, 1),
    binding(autopilot, "Autopilot", flight, 'A'), binding(flaps, "Flaps", flight, 'F'), binding(rocket, "Rocket booster", flight, 'I'),
    binding(eject, "Leave aircraft / eject", flight, 'E'), binding(parachute, "Open parachute", both, 'Q'),
    binding(remote, "Remote aircraft control", both, keys::caps_lock), binding(rear, "Rear view", flight, 'R'),
    binding(cockpit, "Show cockpit", flight, 'L'), binding(ground, "Ground detail", both, 'G'), binding(look, "Look around", flight, keys::num_lock),
    binding(map, "Map", both, 'M'), binding(rescue, "Call rescue", walking, 'V'), binding(messages, "Radio messages", both, 'O'),
    binding(pause, "Pause / options", both, 'P'), binding(bomb, "Drop bomb", flight, 'B'),
    binding(charge, "Plant charge", walking, ';'), binding(detonate, "Detonate charges", walking, 'B'),
    binding(sound, "Sound effects", flight, 'S'),
    // append new controls to preserve indices in existing saved bindings
    binding(fast_forward, "Fast forward (hold)", both, 'T'),
  };
}
std::string binding_name(control_binding const &binding) {
  auto name{[](int const key)->std::string {
    if(key == -1) return "Unbound";
    if(key == 32) return "Space";
    if(key >= 33 && key <= 126) return std::string(1, static_cast<char>(key));
    if(key >= keys::keypad_0 && key <= keys::keypad_9) return "KP" + std::to_string(key - keys::keypad_0);
    if(key == keys::keypad_add) return "KP+";
    if(key == keys::keypad_subtract) return "KP-";
    switch(key) {
    case 262: return "Right"; case 263: return "Left"; case 264: return "Down"; case 265: return "Up";
    case 340: return "Left Shift"; case 341: return "Left Ctrl"; case 342: return "Left Alt";
    case 344: return "Right Shift"; case 345: return "Right Ctrl"; case 346: return "Right Alt";
    case 280: return "Caps Lock"; case 282: return "Num Lock";
    default: return key >= 290 && key <= 314 ? "F" + std::to_string(key - 289) : "Key " + std::to_string(key);
    }
  }};
  std::string result{binding.key < 0 ? "" : name(binding.key)};
  if(binding.alternate >= 0) result += (result.empty() ? "" : " / ") + name(binding.alternate);
  if(binding.mouse >= 0) result += (result.empty() ? "" : " / ") + std::string{"Mouse "} + std::to_string(binding.mouse + 1);
  return result.empty() ? "Unbound" : result;
}
preferences preferences::load(std::filesystem::path const &path) {
  preferences result;
  std::ifstream input{path};
  std::string line;
  while(std::getline(input, line)) {
    std::istringstream row{line}; std::string name;
    row >> name;
    if(name == "resolution") { unsigned int w{}, h{}; if(row >> w >> h && w >= render::resolution_limits::minimum_width && h >= render::resolution_limits::minimum_height && w <= render::resolution_limits::maximum_width && h <= render::resolution_limits::maximum_height) result.scene.resolution = {w, h}; }
    else if(name == "window_resolution") { int value{}; if(row >> value) result.window_resolution = value != 0; }
    else if(name == "fov") { double value{}; if(row >> value && std::isfinite(value)) result.scene.horizontal_fov = std::clamp(value, graphics::scene_settings::minimum_fov, graphics::scene_settings::maximum_fov); }
    else if(name == "draw_distance") { double value{}; if(row >> value && std::isfinite(value) && value >= 0) result.scene.draw_distance = std::min(value, graphics::scene_settings::maximum_draw_distance); }
    else if(name == "ground_points") { unsigned int value{}; if(row >> value) result.scene.ground_points = std::min(value, graphics::scene_settings::maximum_ground_points); }
    else if(name == "mouse_sensitivity") { double value{}; if(row >> value && std::isfinite(value)) result.mouse_sensitivity = std::clamp(value, minimum_mouse_sensitivity, maximum_mouse_sensitivity); }
    else if(name == "invert_mouse") { int value{}; if(row >> value) result.invert_mouse = value != 0; }
    else if(name == "bind") {
      unsigned int index{}; int key{}, alternate{}, mouse{};
      if(row >> index >> key >> alternate >> mouse && index < result.bindings.size() && keys::valid(key) && keys::valid(alternate) && mouse >= -1 && mouse <= 7) {
        auto &binding{result.bindings[index]}; binding.key = key; binding.alternate = alternate; binding.mouse = mouse;
      }
    }
  }
  return result;
}
void preferences::save(std::filesystem::path const &path) const {
  std::ostringstream text;
  text << "# Corncob Redux settings; key codes follow GLFW.\nresolution " << scene.resolution.width << ' ' << scene.resolution.height
    << "\nwindow_resolution " << window_resolution << "\nfov " << scene.horizontal_fov << "\ndraw_distance " << scene.draw_distance
    << "\nground_points " << scene.ground_points << "\nmouse_sensitivity " << mouse_sensitivity << "\ninvert_mouse " << invert_mouse << '\n';
  for(std::size_t i{0}; i < bindings.size(); ++i) {
    auto const &b{bindings[i]}; text << "bind " << i << ' ' << b.key << ' ' << b.alternate << ' ' << b.mouse << '\n';
  }
  auto const contents{text.str()};
  platform::write_binary(path, {reinterpret_cast<uint8_t const*>(contents.data()), contents.size()});
}

} // namespace corncob::game

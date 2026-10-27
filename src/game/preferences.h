#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include "graphics/scene.h"

namespace corncob::game {

enum class control_context { flight, walking, both };
enum class control_id {
  roll_left, roll_right, pitch_up, pitch_down, rudder_left, rudder_right, brakes,
  throttle_up, throttle_down, forward, backward, strafe_left, strafe_right, jump, drop_through,
  gun, pistol, cannon, missile, autopilot, flaps, rocket, eject, parachute, remote,
  rear, cockpit, ground, look, map, rescue, messages, pause, bomb, charge, detonate, sound, fast_forward,
};
struct control_binding {
  control_id id;
  std::string name;
  control_context context;
  int key;
  int alternate{-1};
  int mouse{-1};

  bool matches_key(int const code) const noexcept {
    return code >= 0 && (key == code || alternate == code);
  }
};
std::vector<control_binding> default_bindings();
std::string binding_name(control_binding const &binding);

struct preferences {
  static double constexpr minimum_mouse_sensitivity{1}, maximum_mouse_sensitivity{200};
  graphics::scene_settings scene;
  bool window_resolution{true};
  bool fullscreen{false};
  double music_volume{100.0}, effects_volume{100.0};
  double mouse_sensitivity{24.0}; // Angle units per physical mouse pixel; full turn = 65536.
  bool invert_mouse{false};
  std::vector<control_binding> bindings{default_bindings()};
  static preferences load(std::filesystem::path const &path);
  void save(std::filesystem::path const &path) const;
};

} // namespace corncob::game

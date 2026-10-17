#pragma once

// Portable GLFW key identifiers, kept independent of the windowing library so
// bindings, saved settings and the headless menus use the same vocabulary.
namespace corncob::platform::key {

inline int constexpr unbound{-1}, space{32};
inline int constexpr escape{256}, enter{257}, backspace{259}, insert{260}, erase{261};
inline int constexpr right{262}, left{263}, down{264}, up{265};
inline int constexpr caps_lock{280}, num_lock{282}, first_function{290}, last_help_function{293}, last_function{314};
inline int constexpr keypad_0{320}, keypad_2{322}, keypad_4{324}, keypad_6{326}, keypad_7{327}, keypad_8{328}, keypad_9{329}, keypad_subtract{333}, keypad_add{334};
inline int constexpr left_shift{340}, left_control{341}, left_alt{342}, right_shift{344}, right_control{345}, right_alt{346}, last{348};

constexpr bool valid(int const code) noexcept {
  return code == unbound || (code >= 32 && code <= 96) || code == 161 || code == 162 || (code >= escape && code <= last);
}

} // namespace corncob::platform::key

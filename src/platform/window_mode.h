#pragma once

struct GLFWwindow;

namespace corncob::platform {

class window_mode {
  GLFWwindow &window;
  int x{0}, y{0}, width{640}, height{350};
  bool maximized{false};
public:
  explicit window_mode(GLFWwindow &window);
  void remember_windowed();
  bool fullscreen() const noexcept;
  bool set_fullscreen(bool enabled);
};

} // namespace corncob::platform

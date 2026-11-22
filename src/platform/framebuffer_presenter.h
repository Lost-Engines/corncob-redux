#pragma once

#include "render/framebuffer.h"

struct GLFWwindow;

namespace corncob::platform {

class framebuffer_presenter {
private:
  GLFWwindow &window;
  render::display_aspect aspect;
  unsigned int texture{0};
  unsigned int texture_width{render::screen_layout::width};
  unsigned int texture_height{render::screen_layout::height};

public:
  framebuffer_presenter(GLFWwindow &window, render::display_aspect aspect);
  ~framebuffer_presenter();
  framebuffer_presenter(framebuffer_presenter const &) = delete;
  framebuffer_presenter &operator=(framebuffer_presenter const &) = delete;
  framebuffer_presenter(framebuffer_presenter &&) = delete;
  framebuffer_presenter &operator=(framebuffer_presenter &&) = delete;
  void present(render::framebuffer const &source);
};

} // namespace corncob::platform

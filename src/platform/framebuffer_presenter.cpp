#include "framebuffer_presenter.h"
#include <stdexcept>
#include <GLFW/glfw3.h>
#ifdef _WIN32
#include <GL/glext.h>
#endif // _WIN32

namespace corncob::platform {

framebuffer_presenter::framebuffer_presenter(GLFWwindow &window, render::display_aspect const aspect) :
  window{window},
  aspect{aspect} {
  /// Allocate a nearest-filtered texture; OpenGL only presents the CPU-rendered framebuffer
  if(glfwGetCurrentContext() != &window) throw std::logic_error{"Corncob presenter requires its window's current GL context"};
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, render::screen_layout::width, render::screen_layout::height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  if(glGetError() != GL_NO_ERROR) {
    glDeleteTextures(1, &texture);
    throw std::runtime_error{"cannot allocate the Corncob framebuffer texture"};
  }
  glDisable(GL_DITHER);
  glEnable(GL_TEXTURE_2D);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
}

framebuffer_presenter::~framebuffer_presenter() {
  /// Delete the presentation texture before the window's context is destroyed
  glDeleteTextures(1, &texture);
}

void framebuffer_presenter::present(render::framebuffer const &source) {
  /// Use the active video mode, keeping enlarged pixels uniform unless CRT aspect correction was requested.
  int width{0};
  int height{0};
  glfwGetFramebufferSize(&window, &width, &height);
  if(width == 0 || height == 0) return;
  auto const viewport{render::fit_viewport(width, height, static_cast<int>(source.width), static_cast<int>(source.height), aspect)};
  glViewport(0, 0, width, height);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glViewport(viewport.x, viewport.y, viewport.width, viewport.height);
  glBindTexture(GL_TEXTURE_2D, texture);
  if(source.width != texture_width || source.height != texture_height) {
    texture_width = source.width;
    texture_height = source.height;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<int>(texture_width), static_cast<int>(texture_height), 0, GL_RGBA, GL_UNSIGNED_BYTE, source.pixels.data());
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<int>(source.width), static_cast<int>(source.height), GL_RGBA, GL_UNSIGNED_BYTE, source.pixels.data());
  }
  glBegin(GL_TRIANGLE_STRIP);
  glTexCoord2f(0.0f, 0.0f);
  glVertex2f(-1.0f, 1.0f);
  glTexCoord2f(0.0f, 1.0f);
  glVertex2f(-1.0f, -1.0f);
  glTexCoord2f(1.0f, 0.0f);
  glVertex2f(1.0f, 1.0f);
  glTexCoord2f(1.0f, 1.0f);
  glVertex2f(1.0f, -1.0f);
  glEnd();
  if(glGetError() != GL_NO_ERROR) throw std::runtime_error{"Corncob framebuffer presentation failed"};
  glfwSwapBuffers(&window);
}

} // namespace corncob::platform

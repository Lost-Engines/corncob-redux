#include "window_mode.h"
#include <algorithm>
#include <cstdint>
#include <GLFW/glfw3.h>

namespace corncob::platform {

namespace {
GLFWmonitor *window_monitor(GLFWwindow &window) {
  auto *selected{glfwGetPrimaryMonitor()};
  if(glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) return selected;
  int x{}, y{}, width{}, height{}, count{};
  glfwGetWindowPos(&window,&x,&y); glfwGetWindowSize(&window,&width,&height);
  auto **monitors{glfwGetMonitors(&count)};
  int64_t best{0};
  for(int i{0}; i<count; ++i) {
    int left{}, top{}; glfwGetMonitorPos(monitors[i],&left,&top);
    auto const *mode{glfwGetVideoMode(monitors[i])};
    if(!mode) continue;
    int64_t const overlap_width{std::max<int64_t>(0,std::min<int64_t>(static_cast<int64_t>(x)+width,static_cast<int64_t>(left)+mode->width)-std::max(x,left))};
    int64_t const overlap_height{std::max<int64_t>(0,std::min<int64_t>(static_cast<int64_t>(y)+height,static_cast<int64_t>(top)+mode->height)-std::max(y,top))};
    if(overlap_width*overlap_height > best) { best = overlap_width*overlap_height; selected = monitors[i]; }
  }
  return selected;
}
}

window_mode::window_mode(GLFWwindow &window) : window{window} {
  glfwGetWindowSize(&window,&width,&height);
  remember_windowed();
}
bool window_mode::fullscreen() const noexcept { return glfwGetWindowMonitor(&window) != nullptr; }
void window_mode::remember_windowed() {
  if(fullscreen() || glfwGetWindowAttrib(&window,GLFW_ICONIFIED)) return;
  maximized = glfwGetWindowAttrib(&window,GLFW_MAXIMIZED) != 0;
  if(maximized) return;
  if(glfwGetPlatform() != GLFW_PLATFORM_WAYLAND) glfwGetWindowPos(&window,&x,&y);
  glfwGetWindowSize(&window,&width,&height);
}
bool window_mode::set_fullscreen(bool const enabled) {
  if(enabled == fullscreen()) return true;
  if(enabled) {
    auto *monitor{window_monitor(window)};
    if(!monitor) return false;
    auto const *mode{glfwGetVideoMode(monitor)};
    if(!mode) return false;
    remember_windowed();
    glfwSetWindowMonitor(&window,monitor,0,0,mode->width,mode->height,mode->refreshRate);
  } else {
    glfwSetWindowMonitor(&window,nullptr,x,y,width,height,GLFW_DONT_CARE);
    if(maximized) glfwMaximizeWindow(&window);
  }
  return enabled == fullscreen();
}

} // namespace corncob::platform

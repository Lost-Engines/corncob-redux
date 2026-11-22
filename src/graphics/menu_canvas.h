#pragma once

#include "graphics/interface.h"

namespace corncob::graphics {

class menu_canvas {
private:
  interface_canvas &canvas;
  unsigned int foreground{7};
  unsigned int background{1};
  unsigned int border{14};
  unsigned int emphasis{14};

public:
  static unsigned int constexpr columns{80};
  static unsigned int constexpr rows{25};
  explicit menu_canvas(interface_canvas &canvas);
  void colours(unsigned int foreground, unsigned int background, unsigned int border, unsigned int emphasis = 14);
  void shadow(int left, int top, int right, int bottom);
  void text(int column, int row, std::string_view text, unsigned int colour = 16, unsigned int background = 16);
  void rich_text(int column, int row, std::string_view text, unsigned int colour = 16, unsigned int width = columns);
  void rich_text(int column, int row, std::string_view text, unsigned int colour, unsigned int width, bool &highlighted);
  void panel(int left, int top, int right, int bottom, unsigned int colour = 16, bool double_border = false, bool shadow = true);
  void frame(std::string_view title);
  void heading(std::string_view title);
  void option(int column, int row, std::string_view label, bool selected);
};

} // namespace corncob::graphics

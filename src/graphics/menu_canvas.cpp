#include "menu_canvas.h"
#include <algorithm>
#include "render/palette.h"

namespace corncob::graphics {

menu_canvas::menu_canvas(interface_canvas &canvas) :
  canvas{canvas} {
  /// MOAG's graphics-mode menus use the BIOS 8-by-16 character grid and the sixteen EGA colours
}
void menu_canvas::colours(unsigned int const ink, unsigned int const paper, unsigned int const line, unsigned int const highlight) {
  /// Original nested windows select independent text, paper, border and emphasis attributes
  foreground = ink;
  background = paper;
  border = line;
  emphasis = highlight;
}
void menu_canvas::shadow(int const left, int const top, int const right, int const bottom) {
  /// Keep shadows inside the surrounding double frame
  canvas.text_shadow(std::max(8, left * 8), std::max(80, top * 16), std::min(632, (right + 1) * 8), std::min(384, (bottom + 1) * 16));
}
void menu_canvas::text(int const column, int const row, std::string_view const value, unsigned int const colour, unsigned int const background) {
  /// Opaque character cells also erase an earlier selection or a longer dynamic value
  canvas.rectangle(column * 8, row * 16, (column + static_cast<int>(value.size())) * 8, (row + 1) * 16, render::ega_palette.at(background == 16 ? this->background : background));
  canvas.text(column * 8, row * 16, value, render::ega_palette.at(colour == 16 ? foreground : colour));
}
void menu_canvas::rich_text(int const column, int const row, std::string_view const value, unsigned int const colour, unsigned int const width) {
  /// Menu labels start a fresh emphasis span; text readers can retain it across consecutive physical lines
  bool highlighted{false};
  rich_text(column, row, value, colour, width, highlighted);
}
void menu_canvas::rich_text(int column, int const row, std::string_view const value, unsigned int const colour, unsigned int const width, bool &highlighted) {
  /// Original text marks emphasis with backslash-parentheses and single highlighted shortcut characters
  unsigned int drawn{0};
  for(unsigned int i{0}; i < value.size() && drawn < width; ++i) {
    char const ch{value[i]};
    if(ch == '\\' && i + 1 < value.size()) {
      char const next{value[++i]};
      if(next == '\\') {
        text(column++, row, std::string_view{&value[i], 1}, highlighted ? this->emphasis : colour);
        ++drawn;
        continue;
      }
      if(next == '(') {
        highlighted = true;
        continue;
      }
      if(next == ')') {
        highlighted = false;
        continue;
      }
      text(column++, row, std::string_view{&value[i], 1}, 14);
    } else {
      text(column++, row, std::string_view{&value[i], 1}, highlighted ? this->emphasis : colour);
    }
    ++drawn;
  }
}
void menu_canvas::panel(int const left, int const top, int const right, int const bottom, unsigned int const colour, bool const double_border, bool const with_shadow) {
  /// Inclusive character coordinates retain the original CP437 joins and their half-cell border offsets
  if(with_shadow) shadow(left + 1, top + 1, right + 1, bottom + 1);
  unsigned int const line_colour{colour == 16 ? border : colour};
  canvas.rectangle(left * 8, top * 16, (right + 1) * 8, (bottom + 1) * 16, render::ega_palette[background]);
  char const horizontal{static_cast<char>(double_border ? 205 : 196)};
  char const vertical{static_cast<char>(double_border ? 186 : 179)};
  text(left, top, std::string{static_cast<char>(double_border ? 201 : 218)} + std::string(static_cast<unsigned int>(right - left - 1), horizontal) + static_cast<char>(double_border ? 187 : 191), line_colour);
  text(left, bottom, std::string{static_cast<char>(double_border ? 200 : 192)} + std::string(static_cast<unsigned int>(right - left - 1), horizontal) + static_cast<char>(double_border ? 188 : 217), line_colour);
  for(int row{top + 1}; row < bottom; ++row) {
    text(left, row, std::string_view{&vertical, 1}, line_colour);
    text(right, row, std::string_view{&vertical, 1}, line_colour);
  }
}
void menu_canvas::frame(std::string_view const title) {
  /// Preserve MOAG's double outside frame and overlapping title plaque
  canvas.clear();
  panel(0, 1, 79, 24, 14, true, false);
  heading(title);
}
void menu_canvas::heading(std::string_view const title) {
  /// Changing a submenu heading leaves the original underlying main-menu screen intact
  auto const ink{foreground};
  auto const paper{background};
  auto const line{border};
  auto const highlight{emphasis};
  colours(7, 1, 14);
  panel(21, 0, 57, 4, 14, false, false);
  text(21, 1, "\xb5", 14);
  text(57, 1, "\xc6", 14);
  text(34, 1, "Corncob 3D", 15);
  text(21, 2, std::string{"\xc3"} + std::string(35, '\xc4') + '\xb4', 14);
  colours(ink, paper, line, highlight);
  text(22, 3, std::string(35, ' '));
  text(39 - static_cast<int>(title.size()) / 2, 3, title);
}
void menu_canvas::option(int const column, int const row, std::string_view const label, bool const selected) {
  /// The DOS selection brightens the wording; its shortcut stays yellow without adding a modern highlight bar
  rich_text(column, row, label, selected ? 15 : foreground);
}

} // namespace corncob::graphics

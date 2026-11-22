#include "interface.h"
#include <algorithm>
#include <cassert>
#include "render/palette.h"

namespace corncob::graphics {

interface_canvas::interface_canvas(render::framebuffer &target, std::span<uint8_t const> const font, unsigned int const font_height,
                                   unsigned int const width, unsigned int const height) :
  target{target},
  font{font},
  font_height{font_height} {
  /// Menus retain EGA text geometry while original VGA title/outcome panels use their native 320 by 200 pixels
  assert(width <= 640 && height <= 480 && font.size() == font_height * 256);
  target.pixels.resize(width * height);
  target.width = width;
  target.height = height;
}
void interface_canvas::text_shadow(int const left, int const top, int const right, int const bottom) {
  /// Original text-window shadows retain dim character ink on black rather than hiding the underlying text
  auto const background{target.pixels[static_cast<unsigned int>(std::clamp(top, 0, static_cast<int>(target.height) - 1)) * target.width + static_cast<unsigned int>(std::clamp(left, 0, static_cast<int>(target.width) - 1))]};
  for(int y{std::max(0, top)}; y < std::min(bottom, static_cast<int>(target.height)); ++y) {
    for(int x{std::max(0, left)}; x < std::min(right, static_cast<int>(target.width)); ++x) {
      auto &pixel{target.pixels[static_cast<unsigned int>(y) * target.width + static_cast<unsigned int>(x)]};
      bool const blank{(pixel.red == background.red && pixel.green == background.green && pixel.blue == background.blue) || (pixel.red == 0 && pixel.green == 0 && pixel.blue == 0)};
      pixel = render::ega_palette[blank ? 0 : 8];
    }
  }
}
void interface_canvas::clear(render::rgba_pixel const colour) {
  /// Clear only the active video mode's pixel area
  std::fill_n(target.pixels.begin(), target.width * target.height, colour);
}
void interface_canvas::rectangle(int const left, int const top, int const right, int const bottom, render::rgba_pixel const colour) {
  /// Clip menu fills without permitting artwork or text to write outside the logical video mode
  for(int y{std::max(0, top)}; y < std::min<int>(bottom, static_cast<int>(target.height)); ++y) {
    for(int x{std::max(0, left)}; x < std::min<int>(right, static_cast<int>(target.width)); ++x) {
      target.pixels[static_cast<unsigned int>(y) * target.width + static_cast<unsigned int>(x)] = colour;
    }
  }
}
void interface_canvas::box(int const left, int const top, int const right, int const bottom, render::rgba_pixel const colour) {
  /// Thin solid borders reproduce the simple original text/graphics panel framing
  rectangle(left, top, right, top + 1, colour);
  rectangle(left, bottom - 1, right, bottom, colour);
  rectangle(left, top, left + 1, bottom, colour);
  rectangle(right - 1, top, right, bottom, colour);
}
void interface_canvas::text(int const column, int const row, std::string_view const value, render::rgba_pixel const colour, unsigned int const scale) {
  /// Text bytes address the BIOS-compatible CP437 glyphs directly, including original menu borders and symbols
  int x{column}, y{row};
  for(auto const ch : value) {
    if(ch == '\r') continue;
    if(ch == '\n') {
      x = column;
      y += static_cast<int>(font_height * scale);
      continue;
    }
    for(unsigned int line{0}; line != font_height; ++line) {
      auto const bits{font[static_cast<unsigned char>(ch) * font_height + line]};
      for(unsigned int bit{0}; bit != 8; ++bit) if((bits & (128u >> bit)) != 0) {
        int const px{x + static_cast<int>(bit * scale)}, py{y + static_cast<int>(line * scale)};
        rectangle(px, py, px + static_cast<int>(scale), py + static_cast<int>(scale), colour);
      }
    }
    x += static_cast<int>(8 * scale);
  }
}
void interface_canvas::image(resources::indexed_image const &image, int const x, int const y) {
  /// Copy indexed artwork through its actual palette without scaling, filtering or colour conversion guesses
  for(unsigned int row{0}; row != image.height; ++row) {
    for(unsigned int column{0}; column != image.width; ++column) {
      int const px{x + static_cast<int>(column)}, py{y + static_cast<int>(row)};
      if(px < 0 || py < 0 || px >= static_cast<int>(target.width) || py >= static_cast<int>(target.height)) continue;
      target.pixels[static_cast<unsigned int>(py) * target.width + static_cast<unsigned int>(px)] = image.palette[image.pixels[row * image.width + column]];
    }
  }
}
void draw_presentation(render::framebuffer &target, resources::collection const &resources, resources::presentation_panel const &panel) {
  /// Original image placement and BIOS text coordinates are stored in the presentation scripts
  auto const image{resources.image(panel.image)};
  interface_canvas canvas{target, resources.file("font8.bin"), 8, 320, 200};
  canvas.clear(image.palette[0]);
  canvas.image(image, panel.x, panel.y);
  for(auto const &line : panel.lines) canvas.text(static_cast<int>(line.column * 8), static_cast<int>(line.row * 8), line.text, image.palette.at(panel.text_colour));
}

} // namespace corncob::graphics

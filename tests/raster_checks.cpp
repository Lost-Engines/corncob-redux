#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include "graphics/raster.h"
#include "tests/reference/native_discs.h"
#include "tests/reference/native_polygons.h"
#include "tests/reference_reader.h"

namespace {

uint64_t checksum(corncob::graphics::indexed_framebuffer const &source) {
  /// Match the reference checksum over every indexed pixel, including pixels beyond the view bounds
  uint64_t value{14'695'981'039'346'656'037ull};
  for(auto const pixel : source.pixels) value = (value ^ pixel) * 1'099'511'628'211ull;
  return value;
}

} // anonymous namespace

auto main(int const argc, char const *const argv[])->int {
  if(argc != 3) throw std::invalid_argument{"line fixture required"};
  /// Compare original circle sizes and viewport-edge cases with native EGA writes
  corncob::graphics::indexed_framebuffer surface;
  corncob::graphics::random_source random;
  corncob::graphics::raster draw{surface, {
    .left{8},
    .top{4},
    .right{631},
    .bottom{199},
  }, random};
  for(auto const &reference : corncob::tests::native_discs) {
    std::ranges::fill(surface.pixels, 0);
    draw.disc({reference.x, reference.y}, reference.diameter, 12);
    if(checksum(surface) != reference.hash) throw std::runtime_error{"native disc pixels differ at diameter " + std::to_string(reference.diameter)
      + " and centre " + std::to_string(reference.x) + "," + std::to_string(reference.y)};
  }
  unsigned int polygon_index{0};
  for(auto const &reference : corncob::tests::native_polygons) {
    std::ranges::fill(surface.pixels, 0);
    draw.polygon({reference.vertices.data(), reference.count}, 12);
    if(checksum(surface) != reference.hash) {
      throw std::runtime_error{"native polygon pixels differ at case " + std::to_string(polygon_index)};
    }
    ++polygon_index;
  }
  corncob::tests::reference_reader lines{argv[1], "CORNLIN2"};
  unsigned int const line_count{lines.dword()};
  for(unsigned int i{0}; i != line_count; ++i) {
    bool const wire{lines.byte() != 0};
    int const x1{lines.signed_word()}, y1{lines.signed_word()}, x2{lines.signed_word()}, y2{lines.signed_word()};
    uint64_t const low{lines.dword()}, high{lines.dword()};
    std::ranges::fill(surface.pixels, 0);
    if(wire) draw.wire({x1, y1}, {x2, y2}, 12);
    else draw.line({x1, y1}, {x2, y2}, 12);
    if(checksum(surface) != (high << 32 | low)) throw std::runtime_error{"native line pixels differ in case " + std::to_string(i)};
  }
  corncob::tests::reference_reader clips{argv[2], "CORNCLP1"};
  unsigned int const clip_count{clips.dword()};
  corncob::graphics::clip_rectangle constexpr viewport{
    .left{8},
    .top{4},
    .right{631},
    .bottom{199},
  };
  for(unsigned int i{0}; i != clip_count; ++i) {
    int const x1{clips.signed_word()}, y1{clips.signed_word()}, x2{clips.signed_word()}, y2{clips.signed_word()};
    bool const accepted{clips.byte() != 0};
    int const expected_x1{clips.signed_word()}, expected_y1{clips.signed_word()}, expected_x2{clips.signed_word()}, expected_y2{clips.signed_word()};
    corncob::graphics::screen_point start{x1, y1}, end{x2, y2};
    if(corncob::graphics::clip_wire_line(start, end, viewport) != accepted
      || start != corncob::graphics::screen_point{expected_x1, expected_y1} || end != corncob::graphics::screen_point{expected_x2, expected_y2}) {
      throw std::runtime_error{"native wire clipping differs in case " + std::to_string(i)};
    }
  }
  std::cout << "Native disc, polygon and line pixels match, including viewport clipping and degenerate shapes" << std::endl;
  return EXIT_SUCCESS;
}

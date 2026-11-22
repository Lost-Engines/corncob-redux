#include "raster.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include "maths/fixed.h"
#include "graphics/disc_tables.h"

namespace corncob::graphics {

bool clip_wire_line(screen_point &start, screen_point &end, clip_rectangle const &bounds) noexcept {
  /// OCLIP accepts the viewport's final row/column, clips both X endpoints first and then recomputes the Y intersections
  auto region{[&bounds](screen_point const &point){
    return (point.x < bounds.left ? 1u : point.x > bounds.right ? 4u : 0u)
      | (point.y < bounds.top ? 2u : point.y > bounds.bottom ? 8u : 0u);
  }};
  auto first{region(start)};
  auto last{region(end)};
  if((first & last) != 0) return false;
  if((first | last) == 0) return true;
  auto clip_x{[&](screen_point &point, unsigned int const code){
    if((code & 5u) == 0) return;
    int const x{(code & 1u) != 0 ? bounds.left : bounds.right};
    int const dx{end.x - start.x};
    int const dy{end.y - start.y};
    point.y = start.y + static_cast<int>(static_cast<int64_t>(x - start.x) * dy / dx);
    point.x = x;
  }};
  clip_x(start, first);
  clip_x(end, last);
  first = region(start);
  last = region(end);
  if((first & last) != 0) return false;
  auto clip_y{[&](screen_point &point, unsigned int const code){
    if((code & 10u) == 0) return;
    int const y{(code & 2u) != 0 ? bounds.top : bounds.bottom};
    int const dx{end.x - start.x};
    int const dy{end.y - start.y};
    point.x = start.x + static_cast<int>(static_cast<int64_t>(y - start.y) * dx / dy);
    point.y = y;
  }};
  clip_y(start, first);
  clip_y(end, last);
  return true;
}

void raster::raw_pixel(int const x, int const y, colour_index colour, bool const patterned) noexcept {
  if(target.monochrome) colour = render::hercules_colour(colour, x, patterned);
  /// Hardware disc masks can extend beyond their centre-tested view, but never beyond our framebuffer
  if(x < 0 || y < 0 || x >= static_cast<int>(target.width) || y >= static_cast<int>(target.height)) return;
  auto &pixel{target.pixels[static_cast<unsigned int>(y) * target.width + static_cast<unsigned int>(x)]};
  pixel = !target.monochrome && operation == pixel_operation::high_plane ? static_cast<uint8_t>((pixel & 7u) | (colour & 8u)) : static_cast<uint8_t>(colour & 15u);
}
void raster::span(int const y, int const left, int const right, colour_index const colour, uint8_t const mask) noexcept {
  /// Native horizontal spans include the left pixel and exclude the right pixel and bottom border
  // EGA spans compare a whole-row byte address with a viewport address that includes its left byte
  if(y < bounds.top + (bounds.left / 8 != 0 ? 1 : 0) || y >= bounds.bottom) return;
  int const first{std::max(left, bounds.left)};
  int const last{std::min(right, bounds.right)};
  if(first >= last) return;
  auto *const pixels{target.pixels.data() + static_cast<unsigned int>(y) * target.width};
  if(!target.monochrome && mask == 0xff && operation == pixel_operation::replace) {
    std::fill(pixels + first, pixels + last, colour & 15u);
    return;
  }
  for(int x{first}; x != last; ++x) {
    if((mask & (0x80u >> (x & 7))) != 0) raw_pixel(x, y, colour);
  }
}

raster::raster(indexed_framebuffer &target, clip_rectangle const bounds, random_source &random, pixel_operation const operation) :
  target{target},
  bounds{bounds},
  random{random},
  operation{operation} {
  /// Share one indexed CPU surface and the original random stream between viewports
}

void raster::point(screen_point const &position, colour_index const colour) noexcept {
  /// Match the native point routine's exclusive right and bottom clip bounds
  if(position.x < bounds.left || position.x >= bounds.right || position.y < bounds.top || position.y >= bounds.bottom) return;
  raw_pixel(position.x, position.y, colour, false);
}

bool raster::clip_line(screen_point &start, screen_point &end) const noexcept {
  /// Bound projected lines before stepping, including very distant fixed-point projections
  auto region{[this](screen_point const &point){
    unsigned int code{0};
    if(point.x < bounds.left) code |= 1u;
    if(point.x >= bounds.right) code |= 2u;
    if(point.y < bounds.top) code |= 4u;
    if(point.y >= bounds.bottom) code |= 8u;
    return code;
  }};
  for(unsigned int attempt{0}; attempt != 8; ++attempt) {
    auto const first{region(start)};
    auto const last{region(end)};
    if((first & last) != 0) return false;
    if((first | last) == 0) return true;
    auto const outside{first != 0 ? first : last};
    screen_point clipped{0, 0};
    if((outside & (4u | 8u)) != 0) {
      if(start.y == end.y) return false;
      clipped.y = (outside & 4u) != 0 ? bounds.top : bounds.bottom - 1;
      clipped.x = start.x + static_cast<int>(static_cast<int64_t>(end.x - start.x) * (clipped.y - start.y) / (end.y - start.y));
    } else {
      if(start.x == end.x) return false;
      clipped.x = (outside & 1u) != 0 ? bounds.left : bounds.right - 1;
      clipped.y = start.y + static_cast<int>(static_cast<int64_t>(end.y - start.y) * (clipped.x - start.x) / (end.x - start.x));
    }
    if(first != 0) {
      start = clipped;
    } else {
      end = clipped;
    }
  }
  return false;
}

void raster::line(screen_point start, screen_point end, colour_index const colour) noexcept {
  /// EGALINE orders endpoints by X before its decision ties; reversing a segment must not move its half-step pixels
  if(start.x > end.x) {
    screen_point const temporary{start};
    start = end;
    end = temporary;
  }
  if(!clip_line(start, end)) return;
  int const dx{std::abs(end.x - start.x)};
  int const dy{-std::abs(end.y - start.y)};
  int const step_x{start.x < end.x ? 1 : -1};
  int const step_y{start.y < end.y ? 1 : -1};
  int error{dx + dy};
  for(;;) {
    point(start, colour);
    if(start == end) break;
    int const twice{2 * error};
    if(twice >= dy) {
      error += dy;
      start.x += step_x;
    }
    if(twice <= dx) {
      error += dx;
      start.y += step_y;
    }
  }
}

void raster::wire(screen_point start, screen_point end, colour_index const colour) noexcept {
  /// KLINE emits horizontal runs for long shallow wire segments; gauges retain EGALINE's single-pixel decision rule
  if(target.monochrome) {
    // The source selects HGCLINE rather than the colour renderer's KLINE path.
    line(screen_point{start}, screen_point{end}, colour);
    return;
  }
  if(!clip_wire_line(start, end, bounds)) return;
  if(start.x > end.x) {
    screen_point const temporary{start};
    start = end;
    end = temporary;
  }
  int const dx{end.x - start.x};
  int const dy{std::abs(end.y - start.y)};
  int constexpr shallow_line_minimum_width{30};
  if(dy == 0) {
    // KLINE's horizontal span excludes its right endpoint; a zero-length horizontal segment emits nothing
    for(int x{start.x}; x < end.x; ++x) raw_pixel(x, start.y, colour, false);
    return;
  }
  if(dx < shallow_line_minimum_width || dy >= dx) {
    raster{target, {
      .left{bounds.left},
      .top{bounds.top},
      .right{bounds.right + 1},
      .bottom{bounds.bottom + 1},
    }, random, operation}.line(screen_point{start}, screen_point{end}, colour);
    return;
  }
  int const length{dx / dy};
  // native SAR halves the remainder before its 16.16 divide; retain that lost low bit and signed half-unit accumulator
  int const fraction{((dx % dy) >> 1) * 65'536 / dy};
  int16_t decision{0};
  int x{start.x};
  int y{start.y};
  int const vertical_step{start.y < end.y ? 1 : -1};
  for(int row{0}; row != dy; ++row) {
    bool const longer{decision >= 0};
    int const pixels{length + static_cast<int>(longer)};
    for(int n{0}; n != pixels; ++n) raw_pixel(x + n, y, colour, false);
    x += pixels;
    y += vertical_step;
    decision = maths::word(decision + fraction - (longer ? 32'768 : 0));
  }
}

void raster::polygon(std::span<screen_point const> const vertices, colour_index colour) noexcept {
  /// Walk the original two edge chains, retaining its rounding and rejection of non-monotone surfaces
  unsigned int constexpr maximum_vertices{20};
  if(vertices.size() < 3 || vertices.size() > maximum_vertices) return;
  unsigned int first{0};
  int maximum_y{vertices.front().y};
  for(unsigned int i{0}; i != vertices.size(); ++i) {
    if(vertices[i].y <= vertices[first].y) first = i;
    maximum_y = std::max(maximum_y, vertices[i].y);
  }
  int y{vertices[first].y};
  if(y >= bounds.bottom || maximum_y <= bounds.top) return;
  struct edge {
    unsigned int index;
    int end_y;
    int64_t x;
    int64_t slope;
  };
  edge left{
    .index{first},
    .end_y{y},
    .x{0},
    .slope{0},
  };
  edge right{left};
  unsigned int const vertex_count{static_cast<unsigned int>(vertices.size())};
  auto advance{[vertices, vertex_count](edge &current, bool const forward){
    // native edge traversal permits one horizontal segment before a rising edge
    for(unsigned int attempt{0}; attempt != 2; ++attempt) {
      auto const &top{vertices[current.index]};
      current.index = forward ? (current.index + 1) % vertex_count : (current.index + vertex_count - 1) % vertex_count;
      auto const &bottom{vertices[current.index]};
      int const dx{bottom.x - top.x};
      int const dy{bottom.y - top.y};
      if(dy < 0) return false;
      if(dy == 0) continue;
      // cwd followed by xchg puts 0xffff in the fractional word for negative edges
      int64_t const numerator{static_cast<int64_t>(dx) * 65'536 + (dx < 0 ? 65'535 : 0)};
      current.slope = numerator < 0 ? -((-numerator + dy / 2) / dy) : (numerator + dy / 2) / dy;
      current.x = static_cast<int64_t>(top.x) * 65'536 + 0x7fff;
      current.end_y = bottom.y;
      return true;
    }
    return false;
  }};
  if(!advance(left, true) || !advance(right, false)) return;
  bool const stippled{colour > 15};
  if(stippled) colour = static_cast<uint8_t>(random.next() & 0b0101'0101u);
  while(y < maximum_y && y < bounds.bottom) {
    if(y >= left.end_y && !advance(left, true)) return;
    if(y >= right.end_y && !advance(right, false)) return;
    if(y < bounds.top) {
      int const skipped{std::min({left.end_y, right.end_y, bounds.top}) - y};
      left.x += left.slope * skipped;
      right.x += right.slope * skipped;
      y += skipped;
      continue;
    }
    uint8_t const mask{stippled ? static_cast<uint8_t>(random.next() & 0b0101'0101u) : uint8_t{0xff}};
    int const x1{static_cast<int>(left.x >> 16)};
    int const x2{static_cast<int>(right.x >> 16)};
    span(y, std::min(x1, x2), std::max(x1, x2), colour, mask);
    left.x += left.slope;
    right.x += right.slope;
    ++y;
  }
}

void raster::disc(screen_point const &centre, int diameter, colour_index const colour) noexcept {
  /// Translate native DRAWDisc, including its hand-authored small masks and quantised CSQRT lookup
  int constexpr maximum_diameter{1023};
  unsigned int constexpr vertical_aspect{45'397};
  unsigned int constexpr vertical_scale{2936};
  if(diameter < 0) return;
  diameter = std::min(diameter, maximum_diameter);
  if(diameter < static_cast<int>(small_discs.size())) {
    if(centre.x < bounds.left || centre.x >= bounds.right || centre.y < bounds.top || centre.y >= bounds.bottom) return;
    auto const pattern{small_discs[static_cast<unsigned int>(diameter)]};
    for(unsigned int row{0}; row != pattern.row_count; ++row) {
      auto const &source{small_disc_rows[pattern.first_row + row]};
      for(unsigned int bit{0}; bit != 16; ++bit) {
        if((source.mask & (1u << bit)) != 0) raw_pixel(centre.x + 7 - static_cast<int>(bit), centre.y + source.y, colour);
      }
    }
    return;
  }
  unsigned int const height{(static_cast<unsigned int>(diameter) * vertical_aspect) >> 16};
  if(centre.y - static_cast<int>(height / 2) > bounds.bottom || centre.y + static_cast<int>(height / 2) < bounds.top) return;
  uint32_t const scale{static_cast<uint32_t>((static_cast<uint64_t>(vertical_scale) << 16) / static_cast<unsigned int>(diameter))};
  unsigned int const inverse_scale{(scale << 4) >> 16};
  span(centre.y, centre.x - diameter / 2, centre.x + (diameter + 1) / 2 + 1, colour);
  uint32_t scaled_y{0};
  for(unsigned int y{1}; y <= (height + 1) / 2; ++y) {
    scaled_y += scale;
    auto const index{scaled_y >> 16};
    unsigned int const root{index < disc_square_root.size() ? disc_square_root[index] : 0u};
    unsigned int const width{(root << 5) / inverse_scale};
    int const left{centre.x - static_cast<int>(width / 2)};
    int const right{centre.x + static_cast<int>((width + 1) / 2) + 1};
    span(centre.y + static_cast<int>(y), left, right, colour);
    span(centre.y - static_cast<int>(y), left, right, colour);
  }
}

} // namespace corncob::graphics

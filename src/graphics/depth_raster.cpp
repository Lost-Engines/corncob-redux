#include "depth_raster.h"
#include <algorithm>
#include <cmath>

namespace corncob::graphics {

namespace {

// World centimetres are halved in model space: this is a one-metre detail footprint.
// At the original 300-pixel focal length it is one pixel wide at 300 metres.
double constexpr detail_diameter{50.0};

double edge(double ax, double ay, double bx, double by, double x, double y) {
  return (bx - ax) * (y - ay) - (by - ay) * (x - ax);
}

} // anonymous namespace

depth_raster::depth_raster(indexed_framebuffer &target, std::vector<float> &depth, perspective_view view, random_source &random) :
  target{target}, depth{depth}, view{view}, random{random} {
}
view_point depth_raster::orient(view_point const &source) const {
  view_point point{source};
  if(view.rear) { point.x = -point.x; point.y = -point.y; }
  return point;
}
double depth_raster::plane(view_point const &p, unsigned int const index) const {
  // Clip in camera space before dividing, including objects straddling the eye.
  switch(index) {
  case 0: return p.x - 1.0;
  case 1: return (view.centre_x - view.bounds.left) * p.x - view.focal_x * p.y;
  case 2: return (view.bounds.right - view.centre_x) * p.x + view.focal_x * p.y;
  case 3: return (view.centre_y - view.bounds.top) * p.x - view.focal_y * p.z;
  default: return (view.bounds.bottom - view.centre_y) * p.x + view.focal_y * p.z;
  }
}
auto depth_raster::project(view_point const &p) const->projected_point {
  return {view.centre_x - p.y * view.focal_x / p.x, view.centre_y - p.z * view.focal_y / p.x, 1.0 / p.x};
}
double depth_raster::detail_width(double const reciprocal_depth) const {
  return std::clamp(detail_diameter * view.focal_x * reciprocal_depth, 1.0, std::max(1.0, view.pixel_scale));
}
void depth_raster::detail_point(projected_point const &point, colour_index const colour) {
  int const size{static_cast<int>(std::lround(detail_width(point.depth)))};
  // Square footprints preserve the original pixel artwork without filtering the palette.
  int const left{static_cast<int>(std::floor(point.x)) - size / 2}, top{static_cast<int>(std::floor(point.y)) - size / 2};
  for(int y{std::max(top, view.bounds.top)}; y < std::min(top + size, view.bounds.bottom); ++y)
    for(int x{std::max(left, view.bounds.left)}; x < std::min(left + size, view.bounds.right); ++x) pixel(x, y, point.depth, colour);
}
void depth_raster::point(view_point const &position, colour_index const colour) {
  auto const centre{orient(position)};
  if(centre.x < 1.0) return;
  auto const p{project(centre)};
  double const margin{std::max(1.0, view.pixel_scale)};
  if(p.x + margin < view.bounds.left || p.x - margin >= view.bounds.right || p.y + margin < view.bounds.top || p.y - margin >= view.bounds.bottom) return;
  detail_point(p, colour);
}
void depth_raster::pixel(int const x, int const y, double const reciprocal_depth, colour_index colour) {
  if(x < view.bounds.left || x >= view.bounds.right || y < view.bounds.top || y >= view.bounds.bottom) return;
  auto const index{static_cast<std::size_t>(y) * target.width + static_cast<unsigned int>(x)};
  // Later coplanar artwork wins: runway paint, windows and faces keep their authored order.
  if(reciprocal_depth * 1.00001 < depth[index]) return;
  if(colour > 15 && (random.next() & 1u) == 0) return;
  colour &= 15u;
  target.pixels[index] = target.monochrome ? render::hercules_colour(colour, x) : colour;
  depth[index] = static_cast<float>(reciprocal_depth);
}
void depth_raster::triangle(projected_point a, projected_point b, projected_point c, colour_index const colour) {
  double area{edge(a.x, a.y, b.x, b.y, c.x, c.y)};
  if(std::abs(area) < 1e-12) return;
  if(area < 0) { std::swap(b, c); area = -area; }
  int const top{std::max(view.bounds.top, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))))};
  int const bottom{std::min(view.bounds.bottom, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))))};
  double const gradient{(-(c.y - b.y) * a.depth - (a.y - c.y) * b.depth - (b.y - a.y) * c.depth) / area};
  for(int y{top}; y < bottom; ++y) {
    double left{static_cast<double>(view.bounds.right)}, right{static_cast<double>(view.bounds.left)};
    auto intersect{[&](projected_point const &first, projected_point const &last){
      double const row{y + 0.5};
      if((first.y <= row && last.y > row) || (last.y <= row && first.y > row)) {
        double const x{first.x + (last.x - first.x) * (row - first.y) / (last.y - first.y)};
        left = std::min(left, x); right = std::max(right, x);
      }
    }};
    intersect(a, b); intersect(b, c); intersect(c, a);
    int const first{static_cast<int>(std::ceil(std::clamp(left - 0.5, static_cast<double>(view.bounds.left), static_cast<double>(view.bounds.right))))};
    int const last{static_cast<int>(std::floor(std::clamp(right - 0.5, static_cast<double>(view.bounds.left - 1), static_cast<double>(view.bounds.right - 1))))};
    double reciprocal{(edge(b.x, b.y, c.x, c.y, first + 0.5, y + 0.5) * a.depth
      + edge(c.x, c.y, a.x, a.y, first + 0.5, y + 0.5) * b.depth
      + edge(a.x, a.y, b.x, b.y, first + 0.5, y + 0.5) * c.depth) / area};
    for(int x{first}; x <= last; ++x) { pixel(x, y, reciprocal, colour); reciprocal += gradient; }
  }
}
void depth_raster::polygon(std::span<view_point const> const vertices, colour_index const colour, std::optional<double> const surface_depth) {
  double const anchor_depth{surface_depth ? (view.rear ? -*surface_depth : *surface_depth) : 0.0};
  if(surface_depth && anchor_depth < 1.0) return;
  std::vector<view_point> clipped;
  for(auto const &point : vertices) clipped.push_back(orient(point));
  for(unsigned int i{0}; i != 5 && !clipped.empty(); ++i) {
    std::vector<view_point> next;
    for(std::size_t n{0}; n < clipped.size(); ++n) {
      auto const &a{clipped[n]}, &b{clipped[(n + 1) % clipped.size()]};
      double const da{plane(a, i)}, db{plane(b, i)};
      if(da >= 0) next.push_back(a);
      if((da >= 0) != (db >= 0)) next.push_back(a + (b - a) * (da / (da - db)));
    }
    clipped = std::move(next);
  }
  if(clipped.size() < 3) return;
  auto vertex{[&](view_point const &point) {
    auto result{project(point)};
    if(surface_depth) result.depth = 1.0 / anchor_depth;
    return result;
  }};
  auto const a{vertex(clipped.front())};
  for(std::size_t i{1}; i + 1 < clipped.size(); ++i) triangle(a, vertex(clipped[i]), vertex(clipped[i + 1]), colour);
}
void depth_raster::line(view_point a, view_point b, colour_index const colour, std::optional<double> const surface_depth) {
  // attached artwork keeps its projection but shares the parent billboard's depth
  double const anchor_depth{surface_depth ? (view.rear ? -*surface_depth : *surface_depth) : 0.0};
  if(surface_depth && anchor_depth < 1.0) return;
  a = orient(a); b = orient(b);
  double const margin{std::max(1.0, view.pixel_scale) * 0.5};
  for(unsigned int i{0}; i != 5; ++i) {
    // Include the stroke's width when clipping at the viewport edges.
    auto distance{[&](view_point const &p){ return plane(p, i) + (i == 0 ? 0.0 : margin * p.x); }};
    double const da{distance(a)}, db{distance(b)};
    if(da < 0 && db < 0) return;
    if(da < 0) a += (b - a) * (da / (da - db));
    else if(db < 0) b = a + (b - a) * (da / (da - db));
  }
  auto first{project(a)}, last{project(b)};
  if(surface_depth) first.depth = last.depth = 1.0 / anchor_depth;
  double const maximum_width{std::max(detail_width(first.depth), detail_width(last.depth))};
  if(maximum_width <= 1.0) {
    int const steps{std::max(1, static_cast<int>(std::ceil(std::max(std::abs(last.x - first.x), std::abs(last.y - first.y)))))};
    for(int i{0}; i <= steps; ++i) {
      double const t{static_cast<double>(i) / steps};
      pixel(static_cast<int>(std::floor(first.x + (last.x - first.x) * t)), static_cast<int>(std::floor(first.y + (last.y - first.y) * t)), first.depth + (last.depth - first.depth) * t, colour);
    }
    return;
  }
  if(std::abs(last.x - first.x) + std::abs(last.y - first.y) < 1e-8) {
    first.depth = std::max(first.depth, last.depth);
    detail_point(first, colour);
    return;
  }
  // Walk a narrow ribbon along the major axis, visiting each covered pixel once.
  // Perspective depth determines both the local thickness and its occlusion.
  bool const steep{std::abs(last.y - first.y) > std::abs(last.x - first.x)};
  if(steep) { std::swap(first.x, first.y); std::swap(last.x, last.y); }
  if(first.x > last.x) std::swap(first, last);
  double const dx{last.x - first.x}, dy{last.y - first.y}, length_squared{dx * dx + dy * dy};
  double const radius{maximum_width * 0.5}, ribbon{radius * std::sqrt(1.0 + dy * dy / (dx * dx))};
  int const major_begin{steep ? view.bounds.top : view.bounds.left}, major_end{steep ? view.bounds.bottom : view.bounds.right};
  int const minor_begin{steep ? view.bounds.left : view.bounds.top}, minor_end{steep ? view.bounds.right : view.bounds.bottom};
  int const begin{std::max(major_begin, static_cast<int>(std::floor(first.x - radius)))};
  int const end{std::min(major_end, static_cast<int>(std::ceil(last.x + radius)))};
  for(int major{begin}; major < end; ++major) {
    double const t{std::clamp((major + 0.5 - first.x) / dx, 0.0, 1.0)};
    double const centre{first.y + dy * t}, reciprocal{first.depth + (last.depth - first.depth) * t};
    if(detail_width(reciprocal) < 1.5) {
      if(major + 1.0 < first.x || major > last.x) continue;
      int const minor{static_cast<int>(std::floor(centre))};
      pixel(steep ? minor : major, steep ? major : minor, reciprocal, colour);
      continue;
    }
    int const low{std::max(minor_begin, static_cast<int>(std::floor(centre - ribbon)))};
    int const high{std::min(minor_end, static_cast<int>(std::ceil(centre + ribbon)))};
    for(int minor{low}; minor < high; ++minor) {
      double const along{std::clamp(((major + 0.5 - first.x) * dx + (minor + 0.5 - first.y) * dy) / length_squared, 0.0, 1.0)};
      double const depth{first.depth + (last.depth - first.depth) * along};
      double const width{detail_width(depth)}, x{major + 0.5 - first.x - dx * along}, y{minor + 0.5 - first.y - dy * along};
      double const squared_distance{x * x + y * y}, squared_radius{width * width * 0.25};
      if(squared_distance < squared_radius || (squared_distance == squared_radius && y < 0.0))
        pixel(steep ? minor : major, steep ? major : minor, depth, colour);
    }
  }
}
void depth_raster::disc(view_point centre, double const diameter, colour_index const colour, std::optional<double> const surface_depth) {
  double const anchor_depth{surface_depth ? (view.rear ? -*surface_depth : *surface_depth) : 0.0};
  if(surface_depth && anchor_depth < 1.0) return;
  centre = orient(centre);
  if(centre.x < 1.0) return;
  auto p{project(centre)};
  if(surface_depth) p.depth = 1.0 / anchor_depth;
  double const radius{std::max(0.5, diameter * view.focal_x / centre.x * 0.5)};
  if(p.x + radius < view.bounds.left || p.x - radius >= view.bounds.right || p.y + radius < view.bounds.top || p.y - radius >= view.bounds.bottom) return;
  int const left{static_cast<int>(std::floor(std::max<double>(view.bounds.left, p.x - radius)))};
  int const right{static_cast<int>(std::ceil(std::min<double>(view.bounds.right, p.x + radius)))};
  int const top{static_cast<int>(std::floor(std::max<double>(view.bounds.top, p.y - radius)))};
  int const bottom{static_cast<int>(std::ceil(std::min<double>(view.bounds.bottom, p.y + radius)))};
  if(radius <= 0.5) { pixel(static_cast<int>(p.x), static_cast<int>(p.y), p.depth, colour); return; }
  for(int y{top}; y < bottom; ++y) for(int x{left}; x < right; ++x) {
    double const dx{x + 0.5 - p.x}, dy{y + 0.5 - p.y};
    if(dx * dx + dy * dy <= radius * radius) pixel(x, y, p.depth, colour);
  }
}

} // namespace corncob::graphics

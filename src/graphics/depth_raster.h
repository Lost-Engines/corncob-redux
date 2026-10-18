#pragma once

#include <optional>
#include <span>
#include <vector>
#include "graphics/raster.h"
#include "vectorstorm/vector/vector3.h"

namespace corncob::graphics {

using view_point = vec3<double>;
struct perspective_view {
  clip_rectangle bounds;
  double focal_x;
  double focal_y;
  double centre_x;
  double centre_y;
  bool rear;
  double pixel_scale{1.0}; // Screen size of one pixel in the original graphics mode.
};

// Reciprocal depth keeps precision at the horizon without imposing a far plane.
class depth_raster {
private:
  indexed_framebuffer &target;
  std::vector<float> &depth;
  perspective_view view;
  random_source &random;
  struct projected_point { double x, y, depth; };
  view_point orient(view_point const &source) const;
  double plane(view_point const &point, unsigned int index) const;
  projected_point project(view_point const &point) const;
  double detail_width(double reciprocal_depth) const;
  void detail_point(projected_point const &point, colour_index colour);
  void pixel(int x, int y, double reciprocal_depth, colour_index colour);
  void triangle(projected_point a, projected_point b, projected_point c, colour_index colour);

public:
  depth_raster(indexed_framebuffer &target, std::vector<float> &depth, perspective_view view, random_source &random);
  // surface_depth is the parent billboard's camera-space X, before rear-view orientation
  void polygon(std::span<view_point const> vertices, colour_index colour, std::optional<double> surface_depth = std::nullopt);
  void line(view_point a, view_point b, colour_index colour, std::optional<double> surface_depth = std::nullopt);
  void point(view_point const &position, colour_index colour);
  void disc(view_point centre, double diameter, colour_index colour, std::optional<double> surface_depth = std::nullopt);
};

} // namespace corncob::graphics

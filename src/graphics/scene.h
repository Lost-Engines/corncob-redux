#pragma once

#include <array>
#include <span>
#include "graphics/depth_raster.h"
#include "graphics/instruments.h"
#include "graphics/ground_detail.h"
#include "graphics/raster.h"
#include "maths/fixed.h"
#include "render/framebuffer.h"
#include "resources/cockpit_mask.h"
#include "resources/geometry_bank.h"
#include "world/world.h"

namespace corncob::graphics {

struct camera {
  maths::world_position position{0, 0, 720};
  maths::angles attitude;
  bool cockpit{true};
  bool rear_view{true};
  bool ground_detail{true};
  bool gunsight{false};
  bool map_mode{false};
  maths::world_position map_position{0, 0, 0};
  uint16_t orb_distortion{0};
  vec3<uint16_t> palette_phase{0, 0, 0};
  uint8_t palette_brightness{255};
  resources::particle_animation const *particles{nullptr};
};

struct render_statistics {
  unsigned int objects{0};
  unsigned int polygons{0};
  unsigned int lines{0};
  unsigned int discs{0};
};

struct scene_settings {
  static double constexpr minimum_fov{30}, maximum_fov{150};
  static double constexpr maximum_draw_distance{100000};
  static unsigned int constexpr original_ground_points{80}, maximum_ground_points{20000};
  render::video_size resolution{1280, 700};
  double horizontal_fov{94.0};
  double draw_distance{0.0}; // Kilometres; zero has no far plane.
  unsigned int ground_points{640};
  static double constexpr minimum_ground_range{0.25}, maximum_ground_range{64.0};
  static double constexpr minimum_gunsight_size{0.5}, maximum_gunsight_size{3.0};
  double gunsight_size{1.0}; // Multiplier after scaling from the original 640x350 display.
  double ground_range{8.0}; // Ground-field spread relative to the original altitude-scaled range.
};
using view_parameters = perspective_view;

class scene_renderer {
private:
  resources::geometry_bank &geometry;
  resources::cockpit_mask const &cockpit;
  render::video_mode video;
  scene_settings settings;
  std::vector<float> depth;
  random_source visual_random;
  indexed_framebuffer indexed{};
  std::array<render::rgba_pixel, 16> palette{};
  render_statistics statistics;
  instrument_panel instruments;
  ground_field detail;

  void set_palette(world::theatre const &theatre, maths::world_position const &position);
  void background(view_parameters const &view, maths::rotation const &rotation);
  void draw_ground_and_stars(world::theatre const &theatre, camera const &observer, maths::rotation const &rotation, std::span<view_parameters const> views, random_source &random);
  void draw_model(resources::model_address address, unsigned int octant, maths::rotation const &matrix,
                  view_point const &origin, std::span<view_parameters const> views, random_source &random);

public:
  scene_renderer(resources::geometry_bank &geometry, resources::cockpit_mask const &cockpit, render::video_mode video = render::video_mode::ega);
  void configure(scene_settings const &value);
  void initialise(maths::world_position const &observer, random_source &random);
  indexed_framebuffer const &indexed_frame() const noexcept;
  std::array<render::rgba_pixel, 16> const &colours() const noexcept;
  render_statistics render(world::theatre const &theatre, camera const &observer, unsigned int frame,
                           uint16_t timer, random_source &random, render::framebuffer &output, instrument_telemetry const *telemetry = nullptr);
  render_statistics preview(world::theatre const &theatre, camera const &observer, unsigned int frame,
                            uint16_t timer, random_source const &random, render::framebuffer &output, instrument_telemetry const *telemetry = nullptr) const;
};

} // namespace corncob::graphics

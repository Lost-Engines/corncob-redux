#include "scene.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>
#include "graphics/homing.h"
#include "graphics/radar.h"
#include "render/palette.h"

namespace corncob::graphics {

namespace {

view_parameters constexpr cockpit_view{
  .bounds{
    .left{8},
    .top{4},
    .right{631},
    .bottom{199},
  },
  .focal_x{300},
  .focal_y{200},
  .centre_x{319},
  .centre_y{101},
  .rear{false},
};
view_parameters constexpr rear_view{
  .bounds{
    .left{168},
    .top{254},
    .right{471},
    .bottom{344},
  },
  .focal_x{150},
  .focal_y{100},
  .centre_x{320},
  .centre_y{299},
  .rear{true},
};
view_parameters constexpr full_view{
  .bounds{
    .left{8},
    .top{4},
    .right{631},
    .bottom{345},
  },
  .focal_x{300},
  .focal_y{200},
  .centre_x{319},
  .centre_y{174},
  .rear{false},
};

view_point rotate(maths::rotation const &matrix, view_point const &point) {
  view_point result;
  for(unsigned int i{0}; i != 3; ++i) result[i] = (matrix.rows[i].x * point.x + matrix.rows[i].y * point.y + matrix.rows[i].z * point.z) / 32768.0;
  return result;
}
unsigned int model_octant(view_point const &relative, maths::rotation const &inverse) {
  unsigned int result{0};
  for(unsigned int axis{0}; axis != 3; ++axis) {
    double sum{0};
    for(unsigned int component{0}; component != 3; ++component) sum += relative[component] * inverse.rows[component][axis];
    result = (result << 1) | (sum < 0 ? 1u : 0u);
  }
  return result;
}
struct visible_object {
  world::placed_object const *object;
  resources::model_address model;
  double distance;
  bool ground_first;
  view_point relative;
};
std::optional<visible_object> visibility(world::placed_object const &object, resources::object_definition const &definition,
                                       maths::world_position const &observer, resources::model_levels const &models) {
  auto delta{view_point{object.position} - view_point{observer}};
  return visible_object{
    .object{&object},
    .model{object.type == world::object_type::aircraft && (object.flags & 0x2000u) != 0 ? resources::geometry_bank::experimental_model : models.near},
    .distance{delta.length_sq()},
    .ground_first{object.position.z == 0 && (definition.properties & std::to_underlying(world::object_property::runway)) != 0},
    .relative{delta * 0.5},
  };
}

} // anonymous namespace

void scene_renderer::set_palette(world::theatre const &theatre, maths::world_position const &position) {
  /// Restore EGA colours, then unpack the theatre's native six-bit sky and ground RGB values
  palette = render::ega_palette;
  int const tile_x{std::clamp((position.x - world::theatre::origin) / world::theatre::tile_size, 0, 7)};
  int const tile_y{std::clamp((position.y - world::theatre::origin) / world::theatre::tile_size, 0, 7)};
  uint64_t packed{0};
  for(auto const byte : theatre.tiles[static_cast<unsigned int>(tile_y) * world::theatre::columns + static_cast<unsigned int>(tile_x)].packed_colours) packed = (packed << 8) | byte;
  if(packed != 0) {
    auto component{[packed](unsigned int const shift){
      unsigned int const value{static_cast<unsigned int>((packed >> shift) & 63u)};
      return static_cast<uint8_t>((value << 2) | (value >> 4));
    }};
    palette[2] = {
      .red{component(14)},
      .green{component(8)},
      .blue{component(2)},
    };
    palette[11] = {
      .red{component(32)},
      .green{component(26)},
      .blue{component(20)},
    };
  }
}

void scene_renderer::background(view_parameters const &view, maths::rotation const &rotation) {
  // The horizon is a straight line in each scanline. Fill its two spans instead of testing every pixel ray.
  double const direction{view.rear ? -1.0 : 1.0};
  // The mirror reverses depth, but its horizontal axis matches the forward view.
  double const horizontal{-rotation.rows[1].z * view.focal_y};
  auto fill{[&](int y, int left, int right, uint8_t colour){
    auto first{indexed.pixels.begin() + static_cast<std::ptrdiff_t>(y) * indexed.width + left};
    if(!indexed.monochrome) std::fill_n(first, right - left, colour);
    else for(int x{left}; x < right; ++x) indexed.pixels[static_cast<std::size_t>(y) * indexed.width + static_cast<unsigned int>(x)] = render::hercules_colour(colour, x);
  }};
  for(int y{view.bounds.top}; y < view.bounds.bottom; ++y) {
    double const height{rotation.rows[0].z * view.focal_x * view.focal_y * direction
      - rotation.rows[2].z * (y - view.centre_y) * view.focal_x
      + horizontal * (view.bounds.left - view.centre_x)};
    if(horizontal == 0) { fill(y, view.bounds.left, view.bounds.right, height >= 0 ? 11 : 2); continue; }
    double const crossing{view.bounds.left - height / horizontal};
    int const boundary{static_cast<int>(std::clamp(horizontal > 0 ? std::ceil(crossing) : std::floor(crossing) + 1.0, static_cast<double>(view.bounds.left), static_cast<double>(view.bounds.right)))};
    fill(y, view.bounds.left, boundary, horizontal > 0 ? 2 : 11);
    fill(y, boundary, view.bounds.right, horizontal > 0 ? 11 : 2);
  }
}

void scene_renderer::draw_ground_and_stars(world::theatre const &theatre, camera const &observer, maths::rotation const &rotation,
                                          std::span<view_parameters const> const views, random_source &random) {
  detail.update(observer.position, random);
  if(!observer.ground_detail) return;
  bool const dark{(palette[11].red >> 2) + (palette[11].green >> 2) + (palette[11].blue >> 2) <= 48};
  // These are procedural motion cues: their field already expands with altitude.
  // Grow the ground footprint too, while perspective still reduces the horizon to single pixels.
  double const ground_footprint{std::max(1.0, observer.position.z / 20'000.0)};
  auto draw_point{[&](maths::world_position const &position, bool const star){
    auto delta{view_point{position} - view_point{observer.position}};
    if(star) delta = delta / 256.0;
    auto const transformed{rotate(rotation, delta * 0.5)};
    for(auto const &view : views.first(star ? views.size() : 1)) {
      auto point{transformed};
      if(view.rear) point.x = -point.x;
      if(point.x < 1.0) continue;
      double const x{view.centre_x - point.y * view.focal_x / point.x}, y{view.centre_y - point.z * view.focal_y / point.x};
      if(x < view.bounds.left || x >= view.bounds.right || y < view.bounds.top || y >= view.bounds.bottom) continue;
      if(star) raster{indexed, view.bounds, random}.point({static_cast<int>(x), static_cast<int>(y)}, 15);
      else depth_raster{indexed, depth, view, random}.point(transformed, dark ? 14 : 0, ground_footprint);
    }
  }};
  for(auto const &point : detail.points()) draw_point(point, false);
  if(theatre.sky.stars) {
    for(auto const &star : detail.stars()) draw_point(star, true);
  }
}

void scene_renderer::draw_model(resources::model_address const address, unsigned int const octant, maths::rotation const &matrix,
                                view_point const &origin, std::span<view_parameters const> const views, random_source &random) {
  for(auto const &command : geometry.commands(address, octant)) {
    std::visit([&](auto const &shape){
      using shape_type = std::remove_cvref_t<decltype(shape)>;
      auto vertex{[&](maths::fixed_vector const &point){ return rotate(matrix, view_point{point}) + origin; }};
      for(auto const &view : views) {
        depth_raster draw{indexed, depth, view, random};
        if constexpr(std::is_same_v<shape_type, resources::polygon>) {
          std::vector<view_point> points;
          for(auto const &point : shape.vertices) points.push_back(vertex(point));
          auto const surface_depth{shape.surface_anchor ? std::optional{vertex(*shape.surface_anchor).x} : std::nullopt};
          draw.polygon(points, geometry.colour(shape.colour), surface_depth);
          ++statistics.polygons;
        } else if constexpr(std::is_same_v<shape_type, resources::wireframe>) {
          for(auto const &line : shape.lines) {
            auto const surface_depth{line.surface_anchor ? std::optional{vertex(*line.surface_anchor).x} : std::nullopt};
            for(std::size_t i{1}; i < line.vertices.size(); ++i) {
              draw.line(vertex(line.vertices[i - 1]), vertex(line.vertices[i]), geometry.colour(line.colour), surface_depth);
              ++statistics.lines;
            }
          }
        } else if constexpr(std::is_same_v<shape_type, resources::point_cloud>) {
          for(auto const &point : shape.points) draw.point(vertex(point.position), geometry.colour(point.colour));
        } else if constexpr(std::is_same_v<shape_type, resources::disc_cloud>) {
          for(auto const &disc : shape.discs) {
            auto const surface_depth{disc.surface_anchor ? std::optional{vertex(*disc.surface_anchor).x} : std::nullopt};
            draw.disc(vertex(disc.position), disc.diameter, geometry.colour(disc.colour), surface_depth);
            ++statistics.discs;
          }
        }
      }
    }, geometry.drawing(command.primitive_index));
  }
}

void scene_renderer::configure(scene_settings const &value) {
  settings = value;
  detail.density(settings.ground_points);
  detail.range(settings.ground_range);
}

scene_renderer::scene_renderer(resources::geometry_bank &geometry, resources::cockpit_mask const &cockpit, render::video_mode const video) :
  geometry{geometry},
  cockpit{cockpit},
  video{video} {
  settings.resolution = render::dimensions(video);
  /// Keep immutable world resources outside the renderer and own only frame-local display state
}

void scene_renderer::initialise(maths::world_position const &observer, random_source &random) {
  (void)random;
  detail.density(settings.ground_points);
  detail.range(settings.ground_range);
  detail.update(observer, visual_random);
}

indexed_framebuffer const &scene_renderer::indexed_frame() const noexcept {
  /// The unified ending inherits the last indexed flight page without an RGBA quantization pass
  return indexed;
}
std::array<render::rgba_pixel, 16> const &scene_renderer::colours() const noexcept {
  /// Retain the actual flight palette, including theatre and mindbender changes, when handing off to SRATS
  return palette;
}
render_statistics scene_renderer::render(world::theatre const &theatre, camera const &observer, unsigned int const frame,
                                        uint16_t const timer, random_source &simulation_random, render::framebuffer &output, instrument_telemetry const *const telemetry) {
  /// Keep authored indexed artwork separate from the configurable 3D rendering surface.
  (void)simulation_random;
  auto &random{visual_random};
  auto const size{settings.resolution};
  indexed.width = output.width = size.width;
  indexed.height = output.height = size.height;
  indexed.monochrome = video == render::video_mode::hercules;
  indexed.pixels.resize(size.width * size.height);
  depth.assign(indexed.pixels.size(), 0.0f);
  output.pixels.resize(indexed.pixels.size());
  // Alternative cockpit bitmaps were not preserved: adapt the normal panel while rendering the world at its real selected resolution.
  indexed_framebuffer panel;
  statistics = {};
  geometry.animate(frame);
  if(observer.particles) geometry.animate_particles(*observer.particles);
  set_palette(theatre, observer.position);
  std::ranges::fill(indexed.pixels, 0);
  if(observer.cockpit) {
    for(unsigned int i{0}; i != panel.pixels.size(); ++i) panel.pixels[i] = (cockpit[i / 8] & (0x80u >> (i & 7))) != 0 ? 7 : 0;
    // The original scene overwrites the raw mask's diagonal construction line; adapt only the actual lower panel.
    std::fill_n(panel.pixels.begin(), cockpit_view.bounds.bottom * panel.width, 0);
  }
  std::array<view_parameters, 2> view_storage{observer.cockpit ? cockpit_view : full_view, rear_view};
  double const available{std::min(size.width / 640.0, size.height / 350.0)};
  double const scale{available >= 1.0 ? std::floor(available) : available};
  double const panel_top{size.height - 350.0 * scale};
  double const panel_left{(size.width - 640.0 * scale) * 0.5};
  auto scaled_x{[&](double const x){ return static_cast<int>(std::lround(panel_left + x * scale)); }};
  auto scaled_y{[&](double const y){ return static_cast<int>(std::lround(panel_top + y * scale)); }};
  int const cockpit_top{scaled_y(200)};
  view_storage[0].bounds = {0, 0, static_cast<int>(size.width), observer.cockpit ? cockpit_top : static_cast<int>(size.height)};
  double const focal{size.width * 0.5 / std::tan(settings.horizontal_fov * 3.141592653589793 / 360.0)};
  view_storage[0].focal_x = focal;
  // The render target uses square pixels; DOS's unequal focal lengths compensated for its display.
  view_storage[0].focal_y = focal;
  view_storage[0].centre_x = size.width * 0.5;
  view_storage[0].centre_y = view_storage[0].bounds.bottom * 0.5;
  double const native_width{static_cast<double>(render::dimensions(video).width)};
  view_storage[0].pixel_scale = size.width / native_width;
  view_storage[1] = {
    .bounds{std::max(0, scaled_x(168)), scaled_y(254), std::min(static_cast<int>(size.width), scaled_x(472)), scaled_y(345)},
    .focal_x{150 * scale},
    .focal_y{150 * scale},
    .centre_x{panel_left + 320 * scale},
    .centre_y{panel_top + 299 * scale},
    .rear{true},
    .pixel_scale{640.0 * scale / native_width},
  };
  if(observer.orb_distortion != 0) {
    int16_t const amount{maths::word(observer.orb_distortion)};
    auto const horizontal_phase{static_cast<maths::angle>(maths::cosine(static_cast<maths::angle>(frame * 3111u)))};
    double const distortion_scale{focal / 300.0};
    view_storage[0].focal_x += (maths::multiply(maths::sine(horizontal_phase), amount) - amount / 2) * distortion_scale;
    view_storage[0].focal_y += (maths::multiply(maths::sine(static_cast<maths::angle>(frame << 12)), amount) - amount / 2) * distortion_scale;
  }
  std::span<view_parameters const> const views{view_storage.data(), observer.cockpit && observer.rear_view ? 2u : 1u};
  auto const rotation{maths::make_rotation(observer.attitude)};
  for(auto const &view : views) background(view, rotation);

  draw_ground_and_stars(theatre, observer, rotation, views, random);
  auto sky{theatre.sky};
  if((palette[11].red >> 2) + (palette[11].green >> 2) + (palette[11].blue >> 2) <= 48) sky.sun_colour = 15;
  geometry.configure_sun(sky);
  maths::angle const sun_angle{theatre.sky.sun_angle};
  maths::fixed_vector const sun_relative{0, maths::multiply(maths::negate(maths::sine(sun_angle)), 1000), maths::multiply(maths::cosine(sun_angle), 1000)};
  auto const sun_origin{rotate(rotation, view_point{sun_relative})};
  draw_model(resources::geometry_bank::sun_model, 0, rotation, sun_origin, views, random);

  // The sun is infinitely distant: its billboard must not occlude scenery.
  std::ranges::fill(depth, 0.0f);
  std::vector<visible_object> visible;
  for(auto const &object : theatre.objects) {
    if(!object.has(world::object_flag::present)) continue;
    double const distance{(view_point{object.position} - view_point{observer.position}).length_sq()};
    if(settings.draw_distance > 0 && distance > settings.draw_distance * settings.draw_distance * 1e10) continue;
    if(auto item{visibility(object, geometry.definition(object.type), observer.position, geometry.appearance(object))}) visible.push_back(std::move(*item));
  }
  if(observer.subject) {
    auto const &subject{*observer.subject};
    auto appearance{geometry.appearance(subject)};
    if(subject.type == world::object_type::ground_crew)
      appearance = {resources::geometry_bank::player_model, resources::geometry_bank::player_model, resources::geometry_bank::player_model};
    if(auto item{visibility(subject, geometry.definition(subject.type), observer.position, appearance)}) visible.push_back(*item);
  }
  if(!observer.map_mode) std::ranges::stable_sort(visible, [](visible_object const &lhs, visible_object const &rhs){
    // Paint ground surfaces first; depth testing resolves solid intersections, while authored overlay order survives.
    if(lhs.ground_first != rhs.ground_first) return lhs.ground_first;
    return lhs.distance > rhs.distance;
  });
  if(observer.map_mode) {
    // survey markers blink independently of the stopped actor timers
    geometry.set_map_colours(timer);
  }
  uint8_t const shared_random{observer.particles ? observer.particles->colour() : geometry.colour(0x31b6)};
  bool const radar_active{std::ranges::any_of(theatre.objects, [&observer](auto const &object){
    if(object.type != world::object_type::control_tower || !object.has(world::object_flag::present)) return false;
    auto const delta{vec3<int64_t>{object.position} - vec3<int64_t>{observer.position}};
    if(std::max({std::abs(delta.x), std::abs(delta.y), std::abs(delta.z)}) >= 1'048'576) return false;
    return maths::magnitude({maths::word(static_cast<int32_t>(delta.x >> 6)), maths::word(static_cast<int32_t>(delta.y >> 6)), maths::word(static_cast<int32_t>(delta.z >> 6))}) <= 5200;
  })};
  for(auto const &item : visible) {
    auto const &object{*item.object};
    geometry.prepare_colours(object, frame, timer, shared_random, radar_active);
    auto const inverse{maths::make_inverse_rotation(object.attitude)};
    auto const matrix{geometry.definition(object.type).rotate_below_tier != 0 ? maths::compose(rotation, inverse) : rotation};
    auto const origin{rotate(rotation, item.relative)};
    draw_model(item.model, model_octant(item.relative, inverse), matrix, origin, views, random);
    ++statistics.objects;
  }
  if(observer.map_mode && (observer.position.z >> 16) >= 8) {
    world::placed_object const marker{
      .position{maths::world_position{observer.map_position}},
      .attitude{},
      .type{world::object_type::internal_aircraft},
      .flags{0x131},
    };
    auto definition{geometry.definition(world::object_type::internal_aircraft)};
    auto constexpr address{resources::geometry_bank::map_marker_model};
    definition.models = {address, address, address};
    if(auto const item{visibility(marker, definition, observer.position, definition.models)}) {
      draw_model(address, 0, rotation, rotate(rotation, item->relative), views.first(1), random);
    }
  }
  if(telemetry && !observer.external && telemetry->aircraft.throttle <= 50) {
    auto const &aircraft{telemetry->aircraft};
    auto const offset{maths::transform(maths::make_inverse_rotation(aircraft.attitude), maths::fixed_vector{10, 0, -900})};
    world::placed_object propeller{
      .position{aircraft.position + maths::world_position{offset}},
      .attitude{
        .heading{aircraft.attitude.heading},
        .pitch{aircraft.attitude.pitch},
        .roll{static_cast<maths::angle>(-static_cast<uint32_t>(timer) * (aircraft.throttle - 10) * 2)},
      },
      .type{world::object_type::propeller},
      .flags{1},
    };
    auto const &definition{geometry.definition(propeller.type)};
    if(auto const item{visibility(propeller, definition, observer.position, geometry.appearance(propeller))}) {
      auto const inverse{maths::make_inverse_rotation(propeller.attitude)};
      auto const matrix{maths::compose(rotation, inverse)};
      auto const origin{rotate(rotation, item->relative)};
      draw_model(item->model, model_octant(item->relative, inverse), matrix, origin, views.first(1), random);
    }
  }
  if(observer.gunsight || (telemetry && !observer.external)) {
    auto const &view{views.front()};
    screen_point centre{static_cast<int>(view.centre_x), static_cast<int>(view.centre_y)};
    bool visible{true};
    if(observer.aim_point) {
      auto const aim{rotate(rotation, view_point{*observer.aim_point} - view_point{observer.position})};
      visible = aim.x > 1.0;
      if(visible) {
        double const x{view.centre_x - aim.y * view.focal_x / aim.x}, y{view.centre_y - aim.z * view.focal_y / aim.x};
        visible = x >= view.bounds.left && x < view.bounds.right && y >= view.bounds.top && y < view.bounds.bottom;
        if(visible) centre = {static_cast<int>(x), static_cast<int>(y)};
      }
    }
    if(visible) draw_gunsight(indexed, random, view.bounds, centre, available * settings.gunsight_size);
  }
  if(telemetry) {
    if(observer.cockpit) {
      for(auto const &object : theatre.objects) {
        if(!object.has(world::object_flag::present) || !world::in_active_neighbourhood(object.position, observer.position)) continue;
        bool const kla{object.type == world::object_type::kla && object.timer < 23'900};
        bool const saucer{object.type == world::object_type::saucer || object.type == world::object_type::objective_saucer};
        if(!kla && !saucer) continue;
        if(auto const position{radar_position(object.position, telemetry->aircraft.position, telemetry->aircraft.attitude.heading)}) {
          geometry.prepare_colours(object, frame, timer, shared_random, radar_active);
          radar_dot(panel, random, *position, kla ? 10 : geometry.colour((timer & 512u) != 0 ? 0x68e0 : 0x68e2));
        }
      }
      instruments.draw(panel, random, *telemetry, frame);
      if(telemetry->slalom) {
        raster progress{panel, {0, 0, 640, 350}, random};
        std::array<screen_point, 6> const positions{
          screen_point{495, 214}, screen_point{502, 214}, screen_point{509, 214},
          screen_point{495, 225}, screen_point{502, 225}, screen_point{509, 225},
        };
        for(unsigned int i{0}; i != positions.size(); ++i) {
          auto const &p{positions[i]};
          std::array<screen_point, 4> const cell{screen_point{p.x, p.y}, screen_point{p.x + 6, p.y}, screen_point{p.x + 6, p.y + 9}, screen_point{p.x, p.y + 9}};
          progress.polygon(cell, static_cast<uint8_t>(i < telemetry->slalom_progress ? 12 : 0));
        }
      }
    }
  }
  if(observer.cockpit) {
    for(unsigned int y{0}; y != indexed.height; ++y) for(unsigned int x{0}; x != indexed.width; ++x) {
      bool in_view{false};
      for(auto const &view : views) if(static_cast<int>(x) >= view.bounds.left && static_cast<int>(x) < view.bounds.right
        && static_cast<int>(y) >= view.bounds.top && static_cast<int>(y) < view.bounds.bottom) in_view = true;
      if(in_view) continue;
      int const px{static_cast<int>(std::floor((x - panel_left) / scale))};
      int const py{static_cast<int>(std::floor((y - panel_top) / scale))};
      uint8_t const colour{px >= 0 && px < 640 && py >= 200 && py < 350 ? panel.pixels[static_cast<unsigned int>(py) * 640 + static_cast<unsigned int>(px)] : uint8_t{0}};
      indexed.pixels[y * indexed.width + x] = indexed.monochrome ? static_cast<uint8_t>(colour == 0 ? 0 : 15) : colour;
    }
  }
  if(observer.palette_phase != vec3<uint16_t>{0, 0, 0}) {
    for(auto &colour : palette) for(unsigned int axis{0}; axis != 3; ++axis) {
      auto &channel{axis == 0 ? colour.red : axis == 1 ? colour.green : colour.blue};
      uint8_t const phase{static_cast<uint8_t>((channel >> 2) + (observer.palette_phase[axis] >> 8))};
      uint8_t const value{static_cast<uint8_t>((phase & 64u) != 0 ? 63u - (phase & 63u) : phase & 63u)};
      channel = static_cast<uint8_t>((value << 2) | (value >> 4));
    }
  }
  if(observer.palette_phase == vec3<uint16_t>{0, 0, 0}) render::fade_palette(palette, observer.palette_brightness);
  for(unsigned int i{0}; i != output.pixels.size(); ++i) output.pixels[i] = indexed.monochrome
    ? (indexed.pixels[i] != 0 ? render::rgba_pixel{255, 255, 255} : render::rgba_pixel{0, 0, 0}) : palette[indexed.pixels[i] & 15u];
  return statistics;
}

render_statistics scene_renderer::preview(world::theatre const &theatre, camera const &observer, unsigned int const frame,
                                         uint16_t const timer, random_source const &random, render::framebuffer &output, instrument_telemetry const *const telemetry) const {
  /// Paused and developer previews must not move ground points, damp instruments or consume the simulation's random stream
  scene_renderer preview{*this};
  random_source continuation{random};
  return preview.render(theatre, observer, frame, timer, continuation, output, telemetry);
}

} // namespace corncob::graphics

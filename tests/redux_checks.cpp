#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include "game/front_end.h"
#include "game/preferences.h"
#include "game/session.h"
#include "game/timing.h"
#include "graphics/depth_raster.h"
#include "resources/assets.h"
#include "resources/documentation.h"

namespace {

void require(bool const value, char const *message) {
  if(!value) throw std::runtime_error{message};
}

void depth_checks() {
  namespace graphics = corncob::graphics;
  graphics::indexed_framebuffer frame;
  frame.width = frame.height = 64;
  frame.pixels = std::vector<uint8_t>(4096);
  std::vector<float> depth(4096);
  corncob::maths::random_source random;
  graphics::depth_raster draw{frame, depth, {{0, 0, 64, 64}, 32, 32, 32, 32, false}, random};
  std::array<graphics::view_point, 4> const wall{{{10,-5,-5}, {10,5,-5}, {10,5,5}, {10,-5,5}}};
  std::array<graphics::view_point, 4> const behind{{{20,-4,-4}, {20,4,-4}, {20,4,4}, {20,-4,4}}};
  draw.polygon(wall, 7);
  draw.polygon(behind, 4);
  draw.line({20,-5,0}, {20,5,0}, 2);
  draw.disc({20,0,0}, 2, 1);
  require(frame.pixels[32 * 64 + 32] == 7, "hidden polygons, lines or discs leaked through a wall");
  draw.polygon(wall, 15);
  require(frame.pixels[32 * 64 + 32] == 15, "coplanar authored markings were lost");
  std::fill(depth.begin(), depth.end(), 0);
  std::array<graphics::view_point, 4> const distant{{{1e10,-5e9,-5e9}, {1e10,5e9,-5e9}, {1e10,5e9,5e9}, {1e10,-5e9,5e9}}};
  draw.polygon(distant, 6);
  require(frame.pixels[32 * 64 + 32] == 6, "distant geometry was clipped or lost depth precision");
  std::array<graphics::view_point, 3> const crosses_eye{{{-100,100,100}, {20,-20,-20}, {20,20,-20}}};
  draw.polygon(crosses_eye, 3);
  draw.line({-1e12,1e12,0}, {1e12,0,0}, 5);
}

void detail_size_checks() {
  namespace graphics = corncob::graphics;
  graphics::indexed_framebuffer frame;
  frame.width = 192; frame.height = 128;
  frame.pixels = std::vector<uint8_t>(frame.width * frame.height);
  std::vector<float> depth(frame.pixels.size());
  corncob::maths::random_source random;
  graphics::perspective_view view{{8, 8, 184, 120}, 900, 600, 96, 64, false, 3};
  auto position{[&](double const x, double const y, double const z) {
    return graphics::view_point{z, (view.centre_x - x) * z / view.focal_x, (view.centre_y - y) * z / view.focal_y};
  }};
  auto clear{[&]{ std::ranges::fill(frame.pixels, 0); std::ranges::fill(depth, 0); }};
  auto pixels{[&](uint8_t const colour){ return std::ranges::count(frame.pixels, colour); }};
  graphics::depth_raster draw{frame, depth, view, random};
  draw.point(position(96, 64, 1000), 7);
  require(pixels(7) == 9, "nearby points did not scale to an original pixel at threefold resolution");
  clear(); draw.point(position(96, 64, 22500), 7);
  require(pixels(7) == 4, "middle-distance points did not shrink with perspective");
  clear(); draw.point(position(96, 64, 1e8), 7);
  require(pixels(7) == 1, "distant points did not reduce to one physical pixel");
  clear(); draw.disc(position(96, 64, 1e8), 0.1, 7);
  require(pixels(7) == 1, "detail scaling enlarged distant authored discs");
  for(double const scale : {0.5, 1.0}) {
    clear(); auto native{view}; native.pixel_scale = scale;
    graphics::depth_raster{frame, depth, native, random}.point(position(96, 64, 1000), 7);
    require(pixels(7) == 1, "native or reduced resolutions enlarged source points");
  }
  clear(); auto rear{view}; rear.rear = true; rear.pixel_scale = 2;
  auto const forward{position(96, 64, 1000)};
  graphics::depth_raster{frame, depth, rear, random}.point(-forward, 7);
  require(pixels(7) == 4, "rear-view points did not use their own pixel scale or orientation");
  clear(); draw.line(position(24, 64, 1000), position(168, 64, 1000), 7);
  auto column{[&](int const x) {
    unsigned int count{0};
    for(unsigned int y{0}; y < frame.height; ++y) if(frame.pixels[y * frame.width + static_cast<unsigned int>(x)] == 7) ++count;
    return count;
  }};
  require(column(96) == 3, "nearby model lines stayed one physical pixel wide");
  clear(); draw.line(position(24, 64, 1e8), position(168, 64, 1e8), 7);
  require(column(96) == 1, "far model lines retained enlarged thickness");
  clear(); draw.line(position(24, 64, 15000), position(168, 64, 150000), 7);
  require(column(32) >= 2 && column(160) == 1, "receding line thickness did not vary with perspective depth");
  clear(); draw.line(position(96, 64, 1000), position(96, 64, 1e8), 7);
  require(pixels(7) == 9, "end-on line failed to retain its nearest endpoint thickness");
  clear(); draw.line(position(24, 8, 1000), position(168, 8, 1000), 7);
  require(pixels(7) > 100, "viewport clipping dropped an overlapping wide line");
  clear(); draw.line(position(24, 7.75, 1000), position(168, 7.75, 1000), 7);
  require(pixels(7) > 100, "line clipping ignored width when the centre was off screen");
  for(unsigned int y{0}; y < frame.height; ++y) for(unsigned int x{0}; x < frame.width; ++x)
    if(x < 8 || x >= 184 || y < 8 || y >= 120) require(frame.pixels[y * frame.width + x] == 0, "wide line leaked outside its view");
  clear();
  std::array<graphics::view_point, 4> const wall{{position(30, 20, 100), position(160, 20, 100), position(160, 110, 100), position(30, 110, 100)}};
  draw.polygon(wall, 4);
  draw.point(position(96, 64, 1000), 7);
  draw.line(position(40, 64, 1000), position(150, 64, 1000), 7);
  require(pixels(7) == 0, "enlarged points or lines leaked through foreground geometry");
  draw.line({-1000, 0, 0}, {1000, 0, 0}, 7);
  require(pixels(7) == 9, "a line crossing the eye failed near-plane clipping or nearest-depth selection");
  clear(); draw.line(position(60, 20, 1000), position(85, 100, 1000), 7);
  auto const ascending{frame.pixels};
  require(pixels(7) > 200, "steep nearby lines lost their thickness");
  clear(); draw.line(position(85, 100, 1000), position(60, 20, 1000), 7);
  require(frame.pixels == ascending, "reversing a steep line changed its coverage");
}

void billboard_detail_checks() {
  namespace graphics = corncob::graphics;
  graphics::indexed_framebuffer frame;
  frame.width = frame.height = 128;
  frame.pixels = std::vector<uint8_t>(128 * 128, 14);
  std::vector<float> depth(frame.pixels.size());
  corncob::maths::random_source random;
  graphics::perspective_view view{{0, 0, 128, 128}, 128, 128, 64, 64, false, 2};
  for(bool const rear : {false, true}) {
    view.rear = rear;
    graphics::depth_raster draw{frame, depth, view, random};
    auto point{[&](double const x, double const y, double const z) {
      return graphics::view_point{rear ? -x : x, rear ? -y : y, z};
    }};
    double const surface{rear ? -100.0 : 100.0};
    auto face{[&] {
      std::array<graphics::view_point, 4> const backdrop{{point(95, 30, 30), point(105, -30, 30), point(105, -30, -30), point(95, 30, -30)}};
      draw.polygon(backdrop, 12, surface);
      draw.disc(point(100, 0, 0), 40, 7);
      draw.disc(point(95, 10, 6), 5, 1, surface);
      draw.disc(point(105, -10, 6), 5, 1, surface);
      draw.line(point(95, 10, -10), point(105, -10, -10), 0, surface);
    }};
    std::ranges::fill(frame.pixels, 14); std::ranges::fill(depth, 0);
    face();
    require(frame.pixels[56 * 128 + 50] == 1 && frame.pixels[56 * 128 + 76] == 1, "attached eyes were hidden behind their head billboard");
    require(frame.pixels[77 * 128 + 56] == 0 && frame.pixels[76 * 128 + 72] == 0, "attached mouth was clipped by its head billboard");
    auto const visible{frame.pixels};
    std::array<graphics::view_point, 4> const wall{{point(50, 30, 30), point(50, -30, 30), point(50, -30, -30), point(50, 30, -30)}};
    for(bool const wall_first : {false, true}) {
      std::ranges::fill(frame.pixels, 14); std::ranges::fill(depth, 0);
      if(wall_first) draw.polygon(wall, 4);
      face();
      if(!wall_first) draw.polygon(wall, 4);
      require(std::ranges::all_of(frame.pixels, [](uint8_t const colour){ return colour == 4; }), "billboard artwork leaked through a foreground wall");
    }
    std::ranges::fill(frame.pixels, 14); std::ranges::fill(depth, 0);
    draw.disc(point(100, 0, 0), 40, 7);
    auto const head{frame.pixels};
    draw.disc(point(105, -10, 6), 5, 1, -surface);
    draw.line(point(95, 10, -10), point(105, -10, -10), 0, -surface);
    require(frame.pixels == head && visible != head, "artwork from a parent behind the camera was drawn");
  }
}

void ground_crew_checks() {
  namespace graphics = corncob::graphics;
  namespace maths = corncob::maths;
  namespace world = corncob::world;
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset()};
  auto const cockpit{corncob::resources::decode_cockpit_mask(corncob::resources::cockpit_asset())};
  graphics::scene_renderer renderer{geometry, cockpit};
  renderer.configure({.resolution{1280,700}, .ground_points{0}});
  world::theatre theatre;
  theatre.objects.push_back({.position{0,0,0}, .attitude{}, .type{world::object_type::ground_crew}, .flags{1}});
  corncob::render::framebuffer frame;
  maths::random_source random;
  for(auto const pose : {world::model_pose::normal, world::model_pose::gesture, world::model_pose::shot}) {
    theatre.objects.front().pose = pose;
    for(auto const heading : std::array<maths::angle, 13>{0, 4096, 8192, 12288, 24576, 28672, 32768, 36864, 40960, 45056, 53248, 57344, 61440}) {
      graphics::camera camera{
        .position{-maths::multiply(maths::cosine(heading), 800), -maths::multiply(maths::sine(heading), 800), 520},
        .attitude{.heading{heading}}, .cockpit{false}, .rear_view{false}, .ground_detail{false},
      };
      renderer.render(theatre, camera, 0, 0, random, frame);
      auto const &indexed{renderer.indexed_frame()};
      unsigned int left_eye{0}, right_eye{0}, left_mouth{0}, right_mouth{0};
      for(unsigned int y{320}; y < 380; ++y) for(unsigned int x{600}; x < 680; ++x) {
        auto const colour{indexed.pixels[y * indexed.width + x]};
        if(colour == 1) (x < 640 ? left_eye : right_eye)++;
        if(colour == 0) (x < 640 ? left_mouth : right_mouth)++;
      }
      if(left_eye == 0 || right_eye == 0) throw std::runtime_error{"real ground crew lost an eye: pose " + std::to_string(std::to_underlying(pose)) + ", heading " + std::to_string(heading) + ", pixels " + std::to_string(left_eye) + "/" + std::to_string(right_eye)};
      require(left_mouth > 0 && right_mouth > 0, "real ground crew lost half their mouth in an oblique view");
    }
  }
}

void renderer_checks() {
  namespace graphics = corncob::graphics;
  namespace world = corncob::world;
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset()};
  auto const cockpit{corncob::resources::decode_cockpit_mask(corncob::resources::cockpit_asset())};
  graphics::scene_renderer renderer{geometry, cockpit};
  corncob::render::framebuffer frame;
  corncob::maths::random_source random;
  auto const seed{random.seed()};
  world::theatre theatre;
  theatre.objects.push_back({.position{5'000'000,0,0}, .attitude{}, .type{world::object_type::control_tower}, .flags{1}});
  graphics::camera observer{.position{0,0,720}, .attitude{}, .cockpit{false}, .rear_view{false}, .ground_detail{false}};
  for(auto const &size : {corncob::render::video_size{160,100}, {641,357}, {1920,1080}, {300,900}, {2560,640}}) {
    for(bool const panel : {false, true}) {
      renderer.configure({.resolution{size}, .horizontal_fov{110}, .ground_points{0}});
      observer.cockpit = panel;
      auto const statistics{renderer.render(theatre, observer, 0, 0, random, frame)};
      require(frame.width == size.width && frame.height == size.height && frame.pixels.size() == static_cast<std::size_t>(size.width) * size.height, "arbitrary resolution changed surface size");
      require(statistics.objects == 1, "a distant object was culled by its original tile or LOD range");
    }
  }
  renderer.configure({.resolution{640,350}, .draw_distance{20}, .ground_points{0}});
  require(renderer.render(theatre, observer, 0, 0, random, frame).objects == 0, "configured draw distance was not honoured");
  require(random.seed() == seed, "rendering consumed simulation randomness");
  theatre.objects.front().position.x = 40'000;
  observer.cockpit = false;
  auto grey_pixels{[&](double const fov){
    renderer.configure({.resolution{640,350}, .horizontal_fov{fov}, .ground_points{0}});
    renderer.render(theatre, observer, 0, 0, random, frame);
    return std::ranges::count(renderer.indexed_frame().pixels, uint8_t{7});
  }};
  auto const narrow{grey_pixels(50)}, wide{grey_pixels(120)};
  require(narrow > wide && wide > 0, "FOV did not change projected object size");
  theatre.objects.clear();
  theatre.objects.push_back({.position{0,0,16000}, .attitude{}, .type{world::object_type::control_tower}, .flags{1}});
  theatre.objects.push_back({.position{0,-2000,0}, .attitude{}, .type{world::object_type::tower_doorway}, .flags{0}});
  for(bool const front : {false, true}) {
    observer.position = corncob::maths::world_position{0, front ? -20000 : 20000, 720};
    observer.attitude.heading = front ? 16384 : 49152;
    renderer.configure({.resolution{640,350}, .ground_points{0}});
    theatre.objects.back().flags = 0;
    renderer.render(theatre, observer, 0, 0, random, frame);
    auto const without{renderer.indexed_frame().pixels};
    theatre.objects.back().flags = 1;
    renderer.render(theatre, observer, 0, 0, random, frame);
    bool const different{without != renderer.indexed_frame().pixels};
    require(different == front, "tower doorway occlusion or front visibility is incorrect");
  }

}

void ground_size_checks() {
  namespace graphics = corncob::graphics;
  namespace resources = corncob::resources;
  resources::geometry_bank geometry{resources::engine_data_asset()};
  auto const cockpit{resources::decode_cockpit_mask(resources::cockpit_asset())};
  corncob::world::theatre theatre;
  auto count{[&](int32_t const altitude, unsigned int const scale) {
    graphics::scene_renderer renderer{geometry, cockpit};
    renderer.configure({.resolution{640 * scale, 350 * scale}, .ground_points{80}, .ground_range{1.0}});
    graphics::camera observer{.position{0, 0, altitude}, .attitude{}, .cockpit{false}};
    corncob::maths::random_source random;
    corncob::render::framebuffer frame;
    renderer.render(theatre, observer, 0, 0, random, frame);
    return std::ranges::count(renderer.indexed_frame().pixels, uint8_t{0});
  }};
  auto const native{count(720, 1)}, doubled{count(720, 2)}, quadrupled{count(720, 4)};
  require(native > 10 && doubled >= native * 3 && quadrupled >= native * 12, "ground dots did not preserve their nearby screen-space weight at higher resolutions");
  auto const distant_native{count(1'000'000, 1)}, distant_quadrupled{count(1'000'000, 4)};
  require(distant_native > 10 && distant_quadrupled < distant_native * 2, "distant ground dots did not remain single-pixel details");
}

void ground_range_checks() {
  namespace graphics = corncob::graphics;
  namespace maths = corncob::maths;
  maths::world_position const observer{20'000, -30'000, 720};
  bool extended{false};
  for(uint32_t seed{0}; seed < 100; ++seed) {
    maths::random_source near_random{seed}, far_random{seed};
    maths::world_position near_point{}, far_point{};
    graphics::regenerate_ground_point(near_point, observer, near_random);
    graphics::regenerate_ground_point(far_point, observer, far_random, 8.0);
    for(unsigned int axis{0}; axis < 2; ++axis) {
      auto const near_delta{near_point[axis] - observer[axis]}, far_delta{far_point[axis] - observer[axis]};
      require(std::abs(far_delta - near_delta * 8) < 8, "ground field range did not scale its extent");
      extended = extended || std::abs(far_delta) > 720 * 32;
    }
    require(near_random.seed() == far_random.seed(), "changing ground range changed point-generation randomness");
  }
  require(extended, "expanded ground points never passed the original cutoff");
  graphics::ground_field field;
  maths::random_source random;
  field.initialise(observer, random);
  field.range(8.0); field.initialise(observer, random);
  require(field.points().size() == 80, "ground range changed the point count");
}

void aircraft_exit_checks() {
  namespace game = corncob::game;
  namespace maths = corncob::maths;
  namespace world = corncob::world;
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset()};
  for(uint16_t const variant : {uint16_t{0}, uint16_t{0x1000}, uint16_t{0x2000}}) {
    for(unsigned int heading{0}; heading < 65'536; heading += 8192) {
      world::theatre theatre;
      theatre.start.z = 720;
      theatre.objects.push_back({.position{0, -3000, 720}, .attitude{.heading{static_cast<maths::angle>(heading)}}, .type{world::object_type::aircraft}, .flags{static_cast<uint16_t>(1u | variant)}});
      game::session sortie{geometry, std::move(theatre)};
      sortie.advance({});
      require(sortie.player() == game::player_mode::in_aircraft, "exit fixture did not board its parked aircraft");
      auto const origin{sortie.craft().position};
      sortie.press(game::action::eject);
      require(sortie.player() == game::player_mode::on_foot && sortie.pilot().standing, "grounded exit was treated as airborne ejection");
      auto const delta{sortie.pilot().position - origin};
      auto const local{maths::transform(maths::make_rotation({.heading{static_cast<maths::angle>(heading)}}), {static_cast<int16_t>(delta.x), static_cast<int16_t>(delta.y), 0})};
      require(local.x < (variant == 0x2000 ? -1900 : -1100) && local.y > 1700, "grounded exit did not clear the wing in aircraft-local coordinates");
      require(sortie.pilot().position.z == 720 && sortie.craft().position == origin, "exiting moved the aircraft or lost its ground height");
      sortie.advance({});
      require(sortie.player() == game::player_mode::on_foot, "a grounded exit immediately boarded again");
    }
  }
  world::theatre training; training.training = true;
  game::session airborne{geometry, std::move(training)};
  airborne.relocate({.title{"Ejection check"}, .position{0, 0, 200'000}, .heading{16384}});
  auto const position{airborne.craft().position};
  auto const velocity{airborne.craft().velocity};
  airborne.press(game::action::eject);
  require(!airborne.pilot().standing && airborne.pilot().position == position && airborne.pilot().velocity == velocity, "ground exit offsets changed airborne ejection");
}

void walking_checks() {
  namespace game = corncob::game;
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset()};
  game::session forward{geometry, {}}, diagonal{geometry, {}}, sideways{geometry, {}};
  auto const origin{forward.pilot().position};
  for(unsigned int n{0}; n < 20; ++n) {
    forward.advance({.walk_forward{true}});
    diagonal.advance({.walk_forward{true}, .strafe{1}});
    sideways.advance({.strafe{1}, .look_yaw{100}});
  }
  auto const a{vec3<double>{forward.pilot().position} - vec3<double>{origin}};
  auto const b{vec3<double>{diagonal.pilot().position} - vec3<double>{origin}};
  require(a.length() > 300 && std::abs(a.length() - b.length()) < 40, "walking diagonal speed was not normalised");
  require(sideways.pilot().attitude.heading != 0 && sideways.pilot().position != origin, "strafing and mouse look failed");
  forward.advance({.look_pitch{30000}});
  require(corncob::maths::word(forward.pilot().attitude.pitch) == 16000, "walking pitch wrapped past the vertical limit");
  forward.advance({.jump{true}});
  require(!forward.pilot().standing && forward.pilot().velocity.z > 0, "Space did not jump immediately");
}

void fast_forward_checks() {
  namespace game = corncob::game;
  namespace timing = game::timing;
  namespace world = corncob::world;
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset()};
  world::theatre theatre;
  theatre.objects.push_back({
    .position{4'000'000, 4'000'000, 0},
    .attitude{},
    .type{world::object_type::kla_site},
    .flags{geometry.definition(world::object_type::kla_site).flags},
    .timer{110},
  });
  auto run{[](game::session &session, bool const fast_forward, game::input_state const &input) {
    timing::simulation_clock clock;
    // retain a fractional step while switching speed, just as between window refreshes
    clock.add_time(timing::seconds_per_step / 4, false);
    unsigned int const frames{fast_forward ? 60u : 600u};
    for(unsigned int frame{0}; frame < frames; ++frame) {
      clock.add_time(timing::seconds_per_step / 2, fast_forward);
      while(clock.take_step()) session.advance(input);
    }
  }};
  game::session walking{geometry, theatre}, fast_walking{geometry, theatre};
  auto const origin{walking.pilot().position};
  game::input_state const pedestrian{
    .walk_forward{true},
    .weapons{.gun{true}},
  };
  run(walking, false, pedestrian);
  run(fast_walking, true, pedestrian);
  require(fast_walking.frame_number() == 300 && fast_walking.timer() == 300 * timing::pit_ticks_per_step, "fast-forward did not advance the full simulation clock tenfold");
  require(fast_walking.pilot().position != origin && fast_walking.pilot().position == walking.pilot().position, "accelerated walking differs from normal fixed steps");
  require(fast_walking.random_stream().seed() == walking.random_stream().seed() && fast_walking.results().encode() == walking.results().encode(), "fast-forward changed gameplay randomness or the sortie clock");
  auto const &normal{walking.world().objects}, &fast{fast_walking.world().objects};
  require(normal.size() == fast.size(), "fast-forward changed the object pools");
  bool active_kla{false}, moved_projectile{false};
  for(std::size_t i{0}; i < normal.size(); ++i) {
    auto const &a{normal[i]}, &b{fast[i]};
    require(a.type == b.type && a.position == b.position && a.velocity == b.velocity && a.flags == b.flags && a.timer == b.timer && a.health == b.health, "enemy or projectile updates diverged during fast-forward");
    if(b.type == world::object_type::kla && b.has(world::object_flag::present))
      active_kla = b.timer < 23'900;
    if(b.type == world::object_type::bullets && b.position != origin) moved_projectile = true;
  }
  require(active_kla && fast.front().timer < 400, "KLA production or distant enemy activity slept during fast-forward");
  require(moved_projectile, "projectiles did not move during fast-forward");

  world::theatre training;
  training.training = true;
  game::session flight{geometry, training}, fast_flight{geometry, training};
  game::input_state const aircraft{
    .throttle_up{true},
    .weapons{.gun{true}, .missile{true}},
  };
  flight.set_invincible(true); fast_flight.set_invincible(true);
  run(flight, false, aircraft); run(fast_flight, true, aircraft);
  require(fast_flight.craft().position == flight.craft().position && fast_flight.craft().velocity == flight.craft().velocity && fast_flight.random_stream().seed() == flight.random_stream().seed(), "accelerated flight differs from normal fixed steps");

  timing::simulation_clock clock;
  clock.add_time(timing::seconds_per_step / 2, false);
  require(!clock.take_step(), "normal time advanced before a complete step");
  clock.add_time(timing::seconds_per_step / 2, true);
  unsigned int steps{0};
  while(clock.take_step()) ++steps;
  require(steps == 5, "holding fast-forward discarded fractional time");
  clock.add_time(timing::seconds_per_step / 2, false);
  require(clock.take_step() && !clock.take_step(), "releasing fast-forward did not restore normal speed");
  clock.add_time(timing::seconds_per_step / 2, true);
  clock.reset();
  require(!clock.take_step(), "pausing retained pending fast-forward steps");
}

void proximity_and_crater_checks() {
  namespace world = corncob::world;
  namespace game = corncob::game;
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset()};
  std::array<game::proximity_sound, 2> sounds;
  for(unsigned int order{0}; order < 2; ++order) {
    world::theatre theatre;
    auto const flags{geometry.definition(world::object_type::kla).flags};
    theatre.objects.push_back({.position{10000,0,5000}, .attitude{}, .type{world::object_type::kla}, .flags{static_cast<uint16_t>((flags & ~0x7000u) | 0x1000u)}, .timer{20000}});
    theatre.objects.push_back({.position{300000,0,5000}, .attitude{}, .type{world::object_type::kla}, .flags{static_cast<uint16_t>((flags & ~0x7000u) | 0x7000u)}, .timer{20000}});
    if(order != 0) std::ranges::reverse(theatre.objects);
    game::combat weapons{geometry, theatre}; game::actors actors{geometry, theatre, weapons};
    corncob::maths::world_position const origin{0,0,720}; corncob::maths::fixed_vector const velocity{0,0,0};
    sounds[order] = actors.advance({origin,origin,velocity,false,0}, {5,5}).sound;
  }
  require(sounds[0].kla_distance < 64 && sounds[0].kla_distance == sounds[1].kla_distance && sounds[0].kla_quality == sounds[1].kla_quality, "far KLA overwrote nearest proximity sound");
  world::placed_object crater{.position{0,0,10}, .attitude{}, .type{world::object_type::crater}, .flags{9}, .collision_group{1}, .collision_extents{1000,1000,1000}};
  world::placed_object aircraft{.position{0,0,10}, .attitude{}, .type{world::object_type::aircraft}, .flags{9}, .collision_group{2}, .collision_extents{1000,1000,1000}};
  require(!game::objects_overlap(crater, aircraft), "crater damaged an abandoned aircraft");
  aircraft.type = world::object_type::fuel_dump;
  require(game::objects_overlap(crater, aircraft), "crater blast stopped affecting enemy structures");
  world::theatre theatre;
  crater.flags = 0x31;
  theatre.objects.push_back(crater);
  game::session pedestrian{geometry, std::move(theatre)};
  pedestrian.advance({});
  require(pedestrian.player() == game::player_mode::on_foot, "lingering crater killed a pedestrian");
}

void menu_checks(std::filesystem::path const &scratch) {
  namespace game = corncob::game;
  corncob::resources::collection assets{corncob::resources::front_end_asset()};
  game::campaign_store career{scratch};
  game::front_end front{assets, career, true};
  for(auto const &binding : front.options().bindings) {
    require(!binding.matches_key(-1) && !binding.matches_key(-2), "unknown keyboard events matched an unassigned control");
    if(binding.key >= 0) require(binding.matches_key(binding.key), "primary keyboard binding did not match");
    if(binding.alternate >= 0) require(binding.matches_key(binding.alternate), "alternate keyboard binding did not match");
  }
  front.character('o');
  require(front.current_page() == game::interface_page::control_setup, "options shortcut did not open the new menu");
  front.command(game::interface_command::accept);
  require(front.current_page() == game::interface_page::bindings, "binding list did not open");
  front.options().bindings.front().mouse = 1;
  front.command(game::interface_command::accept);
  require(front.capturing_binding(), "binding capture did not start");
  auto const original_key{front.options().bindings.front().key};
  require(!front.bind_input(-1) && front.capturing_binding(), "an unknown multimedia key ended binding capture");
  require(front.options().bindings.front().key == original_key && front.options().bindings.front().mouse == 1, "an unknown key changed the captured binding");
  require(front.bind_input('K'), "key capture failed");
  require(front.options().bindings.front().key == 'K' && front.options().bindings.front().mouse == 1, "keyboard rebind removed the mouse binding");
  front.command(game::interface_command::accept);
  front.bind_input(-1, 3);
  require(front.options().bindings.front().key == 'K' && front.options().bindings.front().mouse == 3, "mouse rebind removed the keyboard binding");
  auto saved{game::preferences::load(scratch / "redux.cfg")};
  require(saved.bindings.front().key == 'K', "bindings did not persist");
  require(saved.bindings.back().id == game::control_id::fast_forward && saved.bindings.back().key == 'T' && saved.bindings.back().context == game::control_context::both, "T fast-forward was not available while flying and walking");
  {
    std::ofstream legacy{scratch / "legacy.cfg"};
    legacy << "bind 0 75 -1 -1\n";
  }
  auto const legacy{game::preferences::load(scratch / "legacy.cfg")};
  require(legacy.bindings.front().key == 'K' && legacy.bindings.back().key == 'T', "adding fast-forward broke existing settings");
  front.wheel(1000);
  front.wheel(1000);
  front.command(game::interface_command::accept);
  require(front.current_page() == game::interface_page::control_setup, "wheel wrapped past the bottom of the binding list");
  front.command(game::interface_command::home);
  for(unsigned int n{0}; n < 3; ++n) front.command(game::interface_command::down);
  double const old_fov{front.options().scene.horizontal_fov};
  front.command(game::interface_command::next);
  require(front.options().scene.horizontal_fov == old_fov + 2, "FOV menu failed to adjust live settings");
  for(unsigned int n{0}; n < 4; ++n) front.command(game::interface_command::down);
  double const old_sensitivity{front.options().mouse_sensitivity};
  front.command(game::interface_command::next);
  auto const adjusted{game::preferences::load(scratch / "redux.cfg")};
  require(adjusted.scene.horizontal_fov == old_fov + 2 && adjusted.mouse_sensitivity == old_sensitivity + 2, "live FOV or mouse sensitivity failed to save");
  front.command(game::interface_command::down); front.command(game::interface_command::down);
  auto const old_range{front.options().scene.ground_range};
  front.command(game::interface_command::previous);
  auto const ranged{game::preferences::load(scratch / "redux.cfg")};
  require(ranged.scene.ground_range == old_range * 0.5, "ground range menu did not save independently of density");
  require(ranged.scene.ground_points == adjusted.scene.ground_points, "ground range changed the configured density");
  front.command(game::interface_command::cancel);
  front.character('p');
  for(auto const name : {"earth", "other_worlds", "hints", "mission_builder"}) {
    auto const path{scratch / "documentation" / (std::string{name} + ".pdf")};
    require(std::filesystem::file_size(path) > 1000, "PDF export did not write a complete manual");
    require(corncob::resources::documentation_pdf(name).size() == std::filesystem::file_size(path), "exported PDF differs from embedded manual");
  }
  auto const hints{corncob::resources::documentation_text(true, "hints.txt")};
  require(hints.find("toasting") == std::string_view::npos && hints.find("toasted") != std::string_view::npos, "hint typo was not corrected");
  auto const manual{corncob::resources::documentation_text(false, "corncob.doc")};
  require(manual.find("T (hold): tenfold fast-forward") != std::string_view::npos && hints.find("Holding T advances everything") != std::string_view::npos, "manuals omitted restored fast-forward or the production fix");
  require(manual.find("F9: toggle special flag") != std::string_view::npos && manual.find("Space: jump immediately") != std::string_view::npos, "manual omitted editor or walking keys");
}

} // anonymous namespace

int main(int argc, char const *const argv[]) {
  if(argc != 2) throw std::invalid_argument{"scratch directory required"};
  std::filesystem::path const scratch{argv[1]};
  std::filesystem::remove_all(scratch);
  depth_checks(); detail_size_checks(); billboard_detail_checks(); ground_crew_checks(); renderer_checks(); ground_size_checks(); ground_range_checks(); aircraft_exit_checks(); walking_checks(); fast_forward_checks(); proximity_and_crater_checks(); menu_checks(scratch);
  std::filesystem::remove_all(scratch);
  std::cout << "Redux depth, detail sizing, resolution, FOV, walking, fast-forward, bindings, scrolling and PDF checks passed\n";
}

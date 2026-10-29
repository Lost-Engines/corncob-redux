// Optional developer capture tool: real session updates and the production scene renderer.
#ifdef NDEBUG
#error "The scene capture tool is for debug builds only"
#endif
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <boost/program_options.hpp>
#include <png.h>
#include "game/session.h"
#include "resources/assets.h"
#include "resources/cockpit_mask.h"
#include "world/theatre_catalog.h"
#include "world/training.h"

namespace corncob::game {
struct capture_access {
  static void setup(session &g, maths::world_position const &position, maths::angles attitude, bool flying,
                    bool experimental, int16_t speed, uint32_t seed) {
    g.random = maths::random_source{seed};
    g.messages = false;
    g.set_invincible(true);
    g.set_experimental_clearance(true);
    if(flying) {
      auto const index{g.weapons.spawn(world::object_type::aircraft, position)};
      if(!index)
        throw std::runtime_error{"No free aircraft slot"};
      auto &object{g.theatre.objects[*index]};
      object.flags &= static_cast<uint16_t>(~0x104u);
      if(experimental)
        object.flags |= 0x2000;
      g.enter_aircraft(*index);
      g.aircraft.position = position;
      g.aircraft.attitude = attitude;
      g.aircraft.world_to_body = maths::make_rotation(attitude);
      g.aircraft.body_to_world = maths::make_inverse_rotation(attitude);
      g.aircraft.body_velocity = {speed, 0, 0};
      g.aircraft.scaled_body_velocity = {static_cast<int16_t>(speed / 16), 0, 0};
      g.aircraft.velocity = maths::transform(g.aircraft.body_to_world, g.aircraft.body_velocity);
      g.aircraft.throttle = 160;
      g.aircraft.on_ground = position.z <= flight::landing_state::aircraft_height;
      g.landing.stopped = false;
      g.left_ground = !g.aircraft.on_ground;
      g.retain_aircraft();
    } else {
      g.person.position = position;
      g.person.attitude = attitude;
      g.person.surface_height = position.z - flight::landing_state::aircraft_height;
      g.person.standing = true;
    }
    g.enemies.refresh_census(position);
  }
  static void advance(session &g, input_state const &input, double seconds, bool hold) {
    auto const mode{g.mode};
    auto const position{g.aircraft.position};
    auto const attitude{g.aircraft.attitude};
    auto const person{g.person};
    g.advance_time(input, seconds);
    if(hold && mode == g.mode) {
      if(g.mode == player_mode::in_aircraft) {
        g.aircraft.position = position;
        g.aircraft.attitude = attitude;
        g.aircraft.world_to_body = maths::make_rotation(attitude);
        g.aircraft.body_to_world = maths::make_inverse_rotation(attitude);
        g.retain_aircraft();
        g.continuous = {};
      } else {
        g.person = person;
        g.person_fraction = {};
      }
    }
  }
  static void destroy(session &g, unsigned int index) {
    if(index >= g.theatre.objects.size())
      throw std::runtime_error{"Object index out of range"};
    world::placed_object bomb{.position{maths::world_position{g.theatre.objects[index].position}},
                              .attitude{},
                              .type{world::object_type::bomb},
                              .flags{0xc9}};
    g.weapons.damage(index, bomb, true);
  }
  static world::theatre display(session &g, bool external) {
    auto result{g.theatre};
    if(external && g.controlled_aircraft && g.mode == player_mode::in_aircraft) {
      auto &object{result.objects[*g.controlled_aircraft]};
      object.position = g.aircraft.position;
      object.attitude = g.aircraft.attitude;
      object.flags |= std::to_underlying(world::object_flag::present);
    }
    return result;
  }
};
} // namespace corncob::game

namespace {
namespace m = corncob::maths;
namespace g = corncob::game;
namespace w = corncob::world;
namespace gr = corncob::graphics;
namespace po = boost::program_options;
vec3<double> triple(std::string text) {
  std::replace(text.begin(), text.end(), ',', ' ');
  std::istringstream stream{text};
  vec3<double> result{};
  std::string extra;
  if(!(stream >> result.x >> result.y >> result.z) || stream >> extra || !std::isfinite(result.x) ||
     !std::isfinite(result.y) || !std::isfinite(result.z))
    throw std::runtime_error{"Expected three finite numbers: " + text};
  return result;
}
m::world_position position(std::string const &text) {
  auto const value{triple(text)};
  m::world_position result{};
  for(unsigned int i = 0; i < 3; ++i) {
    if(std::abs(value[i]) > 1e6)
      throw std::runtime_error{"Position exceeds 1000 km"};
    result[i] = static_cast<int32_t>(std::lround(value[i] * 100));
  }
  return result;
}
m::angle angle(double degrees) {
  if(!std::isfinite(degrees))
    throw std::runtime_error{"Angle must be finite"};
  return static_cast<m::angle>(std::lround(std::remainder(degrees, 360.0) * 65536.0 / 360));
}
m::angles attitude(std::string const &text) {
  auto const v{triple(text)};
  return {.heading{angle(v.x)}, .pitch{angle(v.y)}, .roll{angle(v.z)}};
}
m::angles towards(m::world_position const &eye, m::world_position const &target, double bank) {
  auto const delta{vec3<double>{target} - vec3<double>{eye}};
  double const degrees{180 / std::numbers::pi};
  return {.heading{angle(std::atan2(delta.y, delta.x) * degrees)},
          .pitch{angle(-std::atan2(delta.z, std::hypot(delta.x, delta.y)) * degrees)},
          .roll{angle(bank)}};
}
void place_object(w::theatre &theatre, std::string const &text, bool copy) {
  auto const first{text.find(':')};
  if(first == std::string::npos)
    throw std::runtime_error{"Object placement requires INDEX:X,Y,Z[:HEADING,PITCH,ROLL]"};
  unsigned int index{};
  std::string extra;
  std::istringstream number{text.substr(0, first)};
  if(!(number >> index) || number >> extra || index >= theatre.objects.size())
    throw std::runtime_error{"Object placement index out of range"};
  auto object{theatre.objects[index]};
  auto const second{text.find(':', first + 1)};
  object.position = position(text.substr(first + 1, second == std::string::npos ? second : second - first - 1));
  if(second != std::string::npos)
    object.attitude = attitude(text.substr(second + 1));
  if(copy)
    theatre.objects.push_back(std::move(object));
  else
    theatre.objects[index] = std::move(object);
}
void png(std::string const &file, corncob::render::framebuffer const &frame) {
  png_image image{};
  image.version = PNG_IMAGE_VERSION;
  image.width = frame.width;
  image.height = frame.height;
  image.format = PNG_FORMAT_RGBA;
  if(!png_image_write_to_file(&image, file.c_str(), 0, frame.pixels.data(), 0, nullptr))
    throw std::runtime_error{image.message};
  png_image_free(&image);
}
} // namespace
int main(int argc, char **argv) try {
  po::options_description options{
      "Debug scene capture (positions in metres, angles in degrees; positive pitch looks down)"};
  auto add{options.add_options()};
  add("help,h", "show options");
  add("list-theatres", "list embedded world identifiers");
  add("output", po::value<std::string>(), "write a PNG using the live Redux renderer");
  add("world", po::value<std::string>()->default_value("morning"), "embedded theatre identifier");
  add("slalom", "use the source-only slalom course");
  add("mission", po::value<unsigned int>(), "start at a zero-based mission index");
  add("eye", po::value<std::string>(), "camera X,Y,Z; defaults to the player after simulation");
  add("at", po::value<std::string>(), "camera looks at X,Y,Z");
  add("track", po::value<unsigned int>(), "move camera and aim with this object during simulation");
  add("list-objects", "print present actors after simulation for composing another capture");
  add("place-object", po::value<std::vector<std::string>>()->composing(), "move an actor: INDEX:X,Y,Z[:HEADING,PITCH,ROLL]; repeatable");
  add("copy-object", po::value<std::vector<std::string>>()->composing(), "copy an actor: INDEX:X,Y,Z[:HEADING,PITCH,ROLL]; repeatable");
  add("pilot", po::value<std::string>(), "player X,Y,Z; defaults to eye or mission start");
  add("attitude", po::value<std::string>()->default_value("0,0,0"), "player/camera heading,pitch,bank");
  add("bank", po::value<double>()->default_value(0), "camera bank when using --at");
  add("flight", "start in an aircraft instead of on foot");
  add("experimental", "use the experimental aircraft");
  add("external", "show the player's aircraft for an external camera");
  add("cockpit", "include the live cockpit instruments");
  add("gunsight", "include the gunsight in a clear view");
  add("speed", po::value<int>()->default_value(5000), "initial original forward-airflow units");
  add("advance", po::value<double>()->default_value(0), "seconds of real simulation before capturing");
  add("hold", "hold player position/attitude while enemies and weapons advance");
  add("fire", po::value<std::string>()->default_value("none"), "none, pistol, cannon, missile, or both");
  add("rocket", "hold rocket boost during simulation");
  add("destroy", po::value<std::string>(), "comma-separated object indices hit by the original damage handler");
  add("destroy-at", po::value<double>()->default_value(0), "time to apply --destroy");
  add("bomb-at", po::value<double>(), "time to drop a bomb");
  add("eject-at", po::value<double>(), "time to leave the aircraft");
  add("parachute-at", po::value<double>(), "time to open the parachute");
  add("rescue-at", po::value<double>(), "time to call the rescue van on foot");
  add("charge-at", po::value<double>(), "time to plant a demolition charge");
  add("detonate-at", po::value<double>(), "time to arm planted charges");
  add("fov", po::value<double>()->default_value(82), "horizontal field of view");
  add("detail", po::value<double>()->default_value(8), "ground density relative to DOS");
  add("range", po::value<double>()->default_value(8), "ground detail range");
  add("size", po::value<std::string>()->default_value("3840x2160"), "WIDTHxHEIGHT");
  add("seed", po::value<uint32_t>()->default_value(2949243), "deterministic simulation seed");
  po::variables_map args;
  po::store(po::parse_command_line(argc, argv, options), args);
  po::notify(args);
  if(args.contains("help")) {
    std::cout << options << '\n';
    return 0;
  }
  if(args.contains("list-theatres")) {
    for(auto const group : {w::theatre_group::earth, w::theatre_group::other_worlds, w::theatre_group::practice})
      for(auto const &t : w::theatres(group))
        std::cout << t.name << "  " << t.title << '\n';
    return 0;
  }
  if(!args.contains("output"))
    throw std::runtime_error{"--output is required"};
  auto finite = [&](char const *key, double low, double high) {
    double v = args[key].as<double>();
    if(!std::isfinite(v) || v < low || v > high)
      throw std::runtime_error{std::string{"Out of range: "} + key};
    return v;
  };
  double const duration{finite("advance", 0, 600)};
  auto settings{gr::scene_settings{}};
  settings.horizontal_fov = finite("fov", 30, 150);
  settings.ground_points = static_cast<unsigned int>(finite("detail", 0, 250) * 80);
  settings.ground_range = finite("range", .25, 64);
  auto size{args["size"].as<std::string>()};
  std::replace(size.begin(), size.end(), 'x', ' ');
  std::istringstream dimensions{size};
  std::string trailing;
  if(!(dimensions >> settings.resolution.width >> settings.resolution.height) || dimensions >> trailing ||
     settings.resolution.width < 160 || settings.resolution.height < 100 || settings.resolution.width > 7680 ||
     settings.resolution.height > 4320)
    throw std::runtime_error{"Invalid resolution"};
  int const speed{args["speed"].as<int>()};
  if(speed < 0 || speed > 32767)
    throw std::runtime_error{"Invalid speed"};
  bool const slalom{args.contains("slalom")};
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset(), slalom};
  auto theatre{slalom ? w::generate_slalom(geometry) : w::load_theatre(args["world"].as<std::string>())};
  for(auto const &[name, copy] : {std::pair{"place-object", false}, {"copy-object", true}})
    if(args.contains(name))
      for(auto const &placement : args[name].as<std::vector<std::string>>())
        place_object(theatre, placement, copy);
  auto start{theatre.start};
  auto pose{attitude(args["attitude"].as<std::string>())};
  if(args.contains("mission")) {
    auto const &mission{theatre.missions.at(args["mission"].as<unsigned int>())};
    start = mission.position;
    if(args["attitude"].defaulted())
      pose.heading = mission.heading;
  }
  if(args.contains("eye"))
    start = position(args["eye"].as<std::string>());
  if(args.contains("pilot"))
    start = position(args["pilot"].as<std::string>());
  if(args.contains("at") && !args.contains("pilot"))
    pose = towards(start, position(args["at"].as<std::string>()), args["bank"].as<double>());
  auto const tracked_start{args.contains("track")
                               ? m::world_position{theatre.objects.at(args["track"].as<unsigned int>()).position}
                               : m::world_position{0, 0, 0}};
  g::session game{geometry, std::move(theatre)};
  g::capture_access::setup(game, start, pose, args.contains("flight"), args.contains("experimental"),
                           static_cast<int16_t>(speed), args["seed"].as<uint32_t>());
  auto const fire{args["fire"].as<std::string>()};
  if(fire != "none" && fire != "pistol" && fire != "cannon" && fire != "missile" && fire != "both")
    throw std::runtime_error{"Invalid --fire"};
  g::input_state const input{.weapons{.gun{fire == "pistol"},
                                      .cannon{fire == "cannon" || fire == "both"},
                                      .missile{fire == "missile" || fire == "both"}}};
  std::vector<unsigned int> destroyed;
  if(args.contains("destroy")) {
    auto s = args["destroy"].as<std::string>();
    std::replace(s.begin(), s.end(), ',', ' ');
    std::istringstream list{s};
    unsigned int id;
    while(list >> id)
      destroyed.push_back(id);
    if(!list.eof())
      throw std::runtime_error{"Invalid object indices"};
  }
  double const damage_time{finite("destroy-at", 0, 600)};
  std::vector<std::pair<double, g::action>> actions;
  for(auto const &[name, command] : {std::pair{"bomb-at", g::action::drop_bomb},
                                     {"eject-at", g::action::eject},
                                     {"parachute-at", g::action::parachute},
                                     {"rescue-at", g::action::call_rescue},
                                     {"charge-at", g::action::plant_charge},
                                     {"detonate-at", g::action::arm_charges}})
    if(args.contains(name))
      actions.emplace_back(finite(name, 0, duration), command);
  std::ranges::sort(actions, {}, &std::pair<double, g::action>::first);
  unsigned int action_index{0};
  bool hit{false};
  for(unsigned int frame = 0; frame <= static_cast<unsigned int>(std::ceil(duration * 60)); ++frame) {
    double const time{frame / 60.0};
    if(!hit && time >= damage_time) {
      for(auto const id : destroyed)
        g::capture_access::destroy(game, id);
      hit = true;
    }
    while(action_index < actions.size() && time >= actions[action_index].first)
      game.press(actions[action_index++].second);
    if(time >= duration)
      break;
    if(args.contains("rocket"))
      game.press(g::action::rocket);
    g::capture_access::advance(game, input, std::min(1.0 / 60, duration - time), args.contains("hold"));
  }
  auto display{g::capture_access::display(game, args.contains("external"))};
  if(args.contains("list-objects"))
    for(unsigned int i = 0; i < display.objects.size(); ++i) {
      auto const &o = display.objects[i];
      if(o.has(w::object_flag::present))
        std::cout << i << " type=" << std::to_underlying(o.type) << " position=" << o.position.x / 100.0 << ','
                  << o.position.y / 100.0 << ',' << o.position.z / 100.0 << '\n';
    }
  auto const tracked_delta{args.contains("track")
                               ? display.objects.at(args["track"].as<unsigned int>()).position - tracked_start
                               : m::world_position{0, 0, 0}};
  auto eye{game.camera()};
  if(args.contains("eye"))
    eye.position = position(args["eye"].as<std::string>());
  eye.position += tracked_delta;
  if(args.contains("at"))
    eye.attitude =
        towards(eye.position, position(args["at"].as<std::string>()) + tracked_delta, args["bank"].as<double>());
  eye.cockpit = args.contains("cockpit");
  eye.gunsight = eye.cockpit || args.contains("gunsight");
  auto const mask{corncob::resources::decode_cockpit_mask(corncob::resources::cockpit_asset())};
  gr::scene_renderer renderer{geometry, mask};
  renderer.configure(settings);
  renderer.initialise(eye.position, game.random_stream());
  corncob::render::framebuffer frame;
  gr::instrument_telemetry const telemetry{.aircraft{game.craft()},
                                           .controls{},
                                           .completed{game.statistics().objectives_completed != 0},
                                           .timer{static_cast<uint16_t>(game.timer())},
                                           .bombs{game.bomb_supply()},
                                           .rocket_colour{game.rocket_indicator_colour()},
                                           .damage{&game.aircraft_damage()},
                                           .slalom{slalom}};
  auto stats{renderer.render(display, eye, game.frame_number(), static_cast<uint16_t>(game.timer()),
                             game.random_stream(), frame, eye.cockpit ? &telemetry : nullptr)};
  png(args["output"].as<std::string>(), frame);
  std::cout << frame.width << 'x' << frame.height << " objects=" << stats.objects << " polygons=" << stats.polygons
            << " explosions=" << game.sounds().explosions << " shots=" << game.sounds().gunshots
            << " missiles=" << game.sounds().missiles << '\n';
  return 0;
} catch(std::exception const &error) {
  std::cerr << error.what() << '\n';
  return 1;
}

#include <algorithm>
#include <bitset>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <boost/program_options.hpp>
#include <boost/scope/scope_exit.hpp>
#include <GLFW/glfw3.h>
#include "audio/effects.h"
#include "audio/music.h"
#include "game/front_end.h"
#include "game/session.h"
#include "game/timing.h"
#include "graphics/scene.h"
#include "platform/audio_output.h"
#include "platform/framebuffer_presenter.h"
#include "platform/joystick.h"
#include "platform/key_codes.h"
#include "platform/storage.h"
#include "platform/window_mode.h"
#include "render/framebuffer.h"
#include "resources/assets.h"
#include "resources/cockpit_mask.h"
#include "resources/geometry_bank.h"
#include "world/airfields.h"
#include "world/theatre_catalog.h"
#include "world/world.h"

namespace {

static_assert(GLFW_KEY_LEFT == corncob::platform::key::left && GLFW_KEY_LAST == corncob::platform::key::last);
using resolution_limits = corncob::render::resolution_limits;
using scene_settings = corncob::graphics::scene_settings;

struct window_state {
  std::unique_ptr<corncob::game::session> &game;
  corncob::game::front_end &front;
  bool &sound_enabled;
  corncob::render::framebuffer &frame;
  std::bitset<GLFW_KEY_LAST + 1> consumed_key_releases{};
  double look_x{0}, look_y{0}, last_x{0}, last_y{0};
  bool mouse_captured{false}, mouse_primed{false};
  double wheel_remainder{0};
  bool changing_window_mode{false};
};

void activate_control(window_state &state, corncob::game::control_id const id) {
  using corncob::game::action;
  using corncob::game::control_id;
  auto &game{*state.game};
  switch(id) {
  case control_id::sound: state.sound_enabled = !state.sound_enabled; break;
  case control_id::autopilot: game.press(action::toggle_autopilot); break;
  case control_id::flaps: game.press(action::toggle_flaps); break;
  case control_id::rocket: game.press(action::rocket); break;
  case control_id::eject: game.press(action::eject); break;
  case control_id::parachute: game.press(action::parachute); break;
  case control_id::remote: game.press(action::toggle_remote); break;
  case control_id::rear: game.press(action::toggle_rear); break;
  case control_id::cockpit: game.press(action::toggle_cockpit); break;
  case control_id::first_person: game.press(action::first_person); break;
  case control_id::third_person: game.press(action::third_person); break;
  case control_id::ground: game.press(action::toggle_ground); break;
  case control_id::look: game.press(action::toggle_look); break;
  case control_id::map: game.press(action::toggle_map); break;
  case control_id::rescue: game.press(action::call_rescue); break;
  case control_id::messages: game.press(action::toggle_messages); break;
  case control_id::pause: state.front.show_menu(); break;
  case control_id::bomb: game.press(action::drop_bomb); break;
  case control_id::charge: game.press(action::plant_charge); break;
  case control_id::detonate: game.press(action::arm_charges); break;
  default: break;
  }
}
bool applies(corncob::game::control_binding const &binding, bool const walking) {
  return binding.context == corncob::game::control_context::both || binding.context == (walking ? corncob::game::control_context::walking : corncob::game::control_context::flight);
}

int startup_failure(std::string const &message) {
  /// Report actionable startup errors while leaving unexpected engine failures uncaught
  std::cerr << "ERROR: " << message << std::endl;
  return EXIT_FAILURE;
}

} // anonymous namespace

auto main(int const argc, char const *const argv[])->int {
  /// Render the embedded original theatre while bringing up the native flight engine
  boost::program_options::options_description options{"Corncob Redux"};
  options.add_options()
    ("help,h", "show command-line options")
    ("version", "show the native reconstruction version")
    ("flight-model", boost::program_options::value<std::string>()->default_value("beginner"), "original flight model: beginner or advanced (default: saved pilot setting, otherwise beginner)")
    ("joystick", boost::program_options::value<std::string>()->default_value("auto"), "flight joystick: auto, none, or a device index 0..15; button 1 guns, button 2 missiles")
    ("list-joysticks", "list connected joystick device indices and names, then exit")
    ("no-music", "start with title and menu music disabled")
    ("no-sound", "start with sound effects disabled")
    ("graphics-mode", boost::program_options::value<std::string>()->default_value("ega"), "flight graphics: ega (640x350), ega-low (320x200), hercules (720x348 monochrome); alternate cockpits are adapted")
    ("resolution", boost::program_options::value<std::string>(), "3D resolution: window (default), or WIDTHxHEIGHT; menus retain their original pixels")
    ("fullscreen", "start fullscreen on the current display")
    ("windowed", "start in a window, overriding a saved fullscreen preference")
    ("gunsight-size", boost::program_options::value<double>(), "gunsight size multiplier, 0.5..3 (default 1, scaled with resolution)")
    ("music-volume", boost::program_options::value<double>(), "music volume, 0..100 percent (default 100)")
    ("effects-volume", boost::program_options::value<double>(), "sound effects volume, 0..100 percent (default 100)")
    ("fov", boost::program_options::value<double>(), "horizontal field of view in degrees, 30..150 (default 94)")
    ("draw-distance", boost::program_options::value<double>(), "maximum visible distance in kilometres; 0 = unlimited (default)")
    ("ground-detail", boost::program_options::value<double>(), "ground point density relative to DOS: 0..250 (default 8)")
    ("ground-detail-range", boost::program_options::value<double>(), "ground field range multiplier: 0.25..64; 1 = DOS range (default 8)")
    ("mouse-sensitivity", boost::program_options::value<double>(), "walking mouse sensitivity, 1..200 (default 24)")
    ("slalom", "launch the recovered source-only slalom course with player weapons disabled")
    ("skip-intro", "open the main menu without the title and story")
    ("theatre", boost::program_options::value<std::string>()->default_value("white_sands"), "world identifier; --list-theatres lists Earth, Other Worlds and practice scenarios")
    ("list-theatres", "list all embedded worlds and exit")
    ("save-dir", boost::program_options::value<std::string>()->default_value(corncob::platform::default_save_directory().string()), "directory for pilot careers and persistent theatres")
    ("aspect", boost::program_options::value<std::string>()->default_value("square"), "pixel aspect: square (uncorrected pixels, like DOSBox aspect=false) or crt (4:3 display)")
    ("scale", boost::program_options::value<int>(), "initial enlargement of the active display (default: largest integer scale that fits)");
#ifndef NDEBUG
  options.add_options()
    ("free-camera", "developer use: inspect scenery with WASD, arrow keys and Page Up/Down; Shift moves faster, Home resets")
    ("no-cockpit", "developer use: show the scene using the original full-screen viewport")
    ("cockpit-preview", "developer use: display the raw cockpit resource instead of scenery")
    ("screenshot", boost::program_options::value<std::string>(), "developer use: save the initial flight scene as PPM and exit without a window")
    ("seconds", boost::program_options::value<double>()->default_value(0.0), "developer use: close after this many seconds; zero disables the time limit");
#endif // NDEBUG
  boost::program_options::variables_map arguments;
  try {
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, options), arguments);
    boost::program_options::notify(arguments);
  } catch(boost::program_options::error const &error) {
    return startup_failure(error.what());
  }
  if(arguments.contains("help")) {
    std::cout << options << std::endl;
    return EXIT_SUCCESS;
  }
  if(arguments.contains("list-theatres")) {
    for(auto const group : {corncob::world::theatre_group::earth, corncob::world::theatre_group::other_worlds, corncob::world::theatre_group::practice}) {
      std::cout << (group == corncob::world::theatre_group::earth ? "Earth" : group == corncob::world::theatre_group::other_worlds ? "Other Worlds" : "Practice / Examples") << ":\n";
      for(auto const &entry : corncob::world::theatres(group)) std::cout << "  " << entry.name << "  " << entry.title << '\n';
    }
    return EXIT_SUCCESS;
  }
  if(arguments.contains("version")) {
    std::cout << "Corncob Redux 0.1.0" << std::endl;
    return EXIT_SUCCESS;
  }
  auto const graphics_argument{arguments["graphics-mode"].as<std::string>()};
  if(graphics_argument != "ega" && graphics_argument != "ega-low" && graphics_argument != "hercules") return startup_failure("--graphics-mode must be ega, ega-low or hercules");
  auto const video{graphics_argument == "ega-low" ? corncob::render::video_mode::ega_low
    : graphics_argument == "hercules" ? corncob::render::video_mode::hercules : corncob::render::video_mode::ega};
  bool const slalom_mode{arguments.contains("slalom")};
  if(slalom_mode && !arguments["theatre"].defaulted()) return startup_failure("--slalom and --theatre select different scenarios; use one");
  auto const flight_model{arguments["flight-model"].as<std::string>()};
  if(flight_model != "advanced" && flight_model != "beginner") return startup_failure("--flight-model must be advanced or beginner");
  int joystick_device{-1};
  auto const joystick_argument{arguments["joystick"].as<std::string>()};
  if(joystick_argument == "none") joystick_device = -2;
  else if(joystick_argument != "auto") {
    auto const result{std::from_chars(joystick_argument.data(), joystick_argument.data() + joystick_argument.size(), joystick_device)};
    if(result.ec != std::errc{} || result.ptr != joystick_argument.data() + joystick_argument.size() || joystick_device < GLFW_JOYSTICK_1 || joystick_device > GLFW_JOYSTICK_LAST)
      return startup_failure("--joystick must be auto, none or a device index from 0 to 15");
  }
  auto const aspect_argument{arguments["aspect"].as<std::string>()};
  if(aspect_argument != "square" && aspect_argument != "crt") return startup_failure("--aspect must be square or crt");
  auto const aspect{aspect_argument == "crt" ? corncob::render::display_aspect::crt : corncob::render::display_aspect::square_pixels};
  bool const automatic_scale{!arguments.contains("scale")};
  int const scale{automatic_scale ? 1 : arguments["scale"].as<int>()};
  if(scale < 1 || scale > std::numeric_limits<int>::max() / static_cast<int>(resolution_limits::maximum_width)) {
    return startup_failure("--scale must be a positive integer whose window dimensions fit in an int");
  }
#ifndef NDEBUG
  double const seconds{arguments["seconds"].as<double>()};
  if(seconds < 0.0 || !std::isfinite(seconds)) return startup_failure("--seconds must be a finite non-negative number");
#endif // NDEBUG
  auto theatre_name{arguments["theatre"].as<std::string>()};
  bool reserve_aircraft{false};
  bool editing_session{false};
  bool demonstration_session{false};
  std::unique_ptr<corncob::game::campaign_store> campaign;
  try {
    campaign = std::make_unique<corncob::game::campaign_store>(arguments["save-dir"].as<std::string>());
  } catch(std::exception const &error) { return startup_failure(error.what()); }
  corncob::world::theatre theatre;
  try {
    campaign->select_campaign(corncob::world::find_theatre(theatre_name).campaign);
    theatre = corncob::world::load_theatre(theatre_name);
    if(auto saved{slalom_mode ? std::nullopt : campaign->load_world(theatre_name)}) {
      saved->sky = theatre.sky;
      saved->environment = theatre.environment;
      saved->briefing = std::move(theatre.briefing);
      saved->missions = std::move(theatre.missions);
      theatre = std::move(*saved);
    }
  } catch(std::exception const &error) {
    return startup_failure(error.what());
  }
  corncob::render::framebuffer frame;
  auto const cockpit{corncob::resources::decode_cockpit_mask(corncob::resources::cockpit_asset())};
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset(), slalom_mode};
  if(slalom_mode) { theatre = corncob::world::generate_slalom(geometry); theatre_name = "training"; }
  auto renderer{std::make_unique<corncob::graphics::scene_renderer>(geometry, cockpit, video)};
  auto game{std::make_unique<corncob::game::session>(geometry, std::move(theatre))};
  renderer->initialise(game->camera().position, game->random_stream());
  corncob::resources::collection interface_assets{corncob::resources::front_end_asset()};
  corncob::game::front_end front{interface_assets, *campaign, arguments.contains("skip-intro"), slalom_mode || !arguments["theatre"].defaulted(), corncob::game::control_setup{
    .joystick_device{arguments["joystick"].defaulted() ? (campaign->keyboard_control() ? -2 : -1) : joystick_device},
    .beginner_flight{arguments["flight-model"].defaulted() ? (!campaign->active_pilot() || campaign->active_pilot()->beginner_flight()) : flight_model == "beginner"},
  }, arguments.contains("no-music") ? std::optional{false} : std::nullopt};
  auto &preferences{front.options()};
  if(arguments.contains("fullscreen") && arguments.contains("windowed")) return startup_failure("use either --fullscreen or --windowed");
  if(arguments.contains("fullscreen")) preferences.fullscreen = true;
  if(arguments.contains("windowed")) preferences.fullscreen = false;
  if(arguments.contains("resolution")) {
    auto const value{arguments["resolution"].as<std::string>()};
    preferences.window_resolution = value == "window";
    if(!preferences.window_resolution) {
      auto const separator{value.find('x')};
      if(separator == std::string::npos) return startup_failure("--resolution must be window or WIDTHxHEIGHT");
      unsigned int width{}, height{};
      auto const w{std::from_chars(value.data(), value.data() + separator, width)};
      auto const h{std::from_chars(value.data() + separator + 1, value.data() + value.size(), height)};
      if(w.ec != std::errc{} || h.ec != std::errc{} || w.ptr != value.data() + separator || h.ptr != value.data() + value.size()
        || width < resolution_limits::minimum_width || height < resolution_limits::minimum_height || width > resolution_limits::maximum_width || height > resolution_limits::maximum_height) return startup_failure("--resolution must be between 160x100 and 7680x4320");
      preferences.scene.resolution = {width, height};
    }
  } else if(!arguments["graphics-mode"].defaulted()) {
    preferences.scene.resolution = corncob::render::dimensions(video);
    preferences.window_resolution = false;
  }
  auto number{[&](char const *name, double &value, double const low, double const high){
    if(!arguments.contains(name)) return true;
    double const requested{arguments[name].as<double>()};
    if(!std::isfinite(requested) || requested < low || requested > high) return false;
    value = requested;
    return true;
  }};
  if(!number("gunsight-size", preferences.scene.gunsight_size, scene_settings::minimum_gunsight_size, scene_settings::maximum_gunsight_size)) return startup_failure("--gunsight-size must be between 0.5 and 3");
  if(!number("music-volume", preferences.music_volume, 0, 100)) return startup_failure("--music-volume must be between 0 and 100");
  if(!number("effects-volume", preferences.effects_volume, 0, 100)) return startup_failure("--effects-volume must be between 0 and 100");
  if(!number("fov", preferences.scene.horizontal_fov, scene_settings::minimum_fov, scene_settings::maximum_fov)) return startup_failure("--fov must be between 30 and 150 degrees");
  if(!number("draw-distance", preferences.scene.draw_distance, 0, scene_settings::maximum_draw_distance)) return startup_failure("--draw-distance must be a finite non-negative number of kilometres");
  if(!number("mouse-sensitivity", preferences.mouse_sensitivity, 1, 200)) return startup_failure("--mouse-sensitivity must be between 1 and 200");
  double density{preferences.scene.ground_points / 80.0};
  if(!number("ground-detail", density, 0, 250)) return startup_failure("--ground-detail must be between 0 and 250");
  preferences.scene.ground_points = static_cast<unsigned int>(std::lround(density * 80));
  if(!number("ground-detail-range", preferences.scene.ground_range, scene_settings::minimum_ground_range, scene_settings::maximum_ground_range)) return startup_failure("--ground-detail-range must be between 0.25 and 64");
  renderer->configure(preferences.scene);
  game->set_experimental_clearance(game->world().expansion || (campaign->active_pilot() && campaign->active_pilot()->awards(corncob::game::medal::combat_master) != 0));
  game->set_beginner_flight(front.flight_controls().beginner_flight);
  if(slalom_mode) { game->set_invincible(true); front.begin_flight(); }
  auto observer{game->camera()};
#ifndef NDEBUG
  bool const no_cockpit{arguments.contains("no-cockpit")};
  if(no_cockpit) observer.cockpit = false;
  bool const cockpit_preview{arguments.contains("cockpit-preview")};
  bool const free_camera{arguments.contains("free-camera")};
  if(free_camera) {
    observer.position = corncob::maths::world_position{game->world().start};
    observer.cockpit = !no_cockpit;
  }
  if(cockpit_preview) corncob::resources::draw_cockpit_preview(cockpit, frame);
  else renderer->preview(game->world(), observer, 0, 0, game->random_stream(), frame);
#else
  renderer->preview(game->world(), observer, 0, 0, game->random_stream(), frame);
#endif // NDEBUG
#ifndef NDEBUG
  if(arguments.contains("screenshot")) {
    std::ofstream file{arguments["screenshot"].as<std::string>(), std::ios::binary};
    file.exceptions(std::ios::failbit | std::ios::badbit);
    file << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
    for(auto const &pixel : frame.pixels) {
      file.put(static_cast<char>(pixel.red));
      file.put(static_cast<char>(pixel.green));
      file.put(static_cast<char>(pixel.blue));
    }
    file.close();
    return EXIT_SUCCESS;
  }
#endif // NDEBUG
#ifndef NDEBUG
  bool const inspect{free_camera || cockpit_preview};
#else
  bool constexpr inspect{false};
#endif // NDEBUG
  if(!front.in_flight() && !inspect) front.render(frame);
  int const display_width{static_cast<int>(frame.width)};
  int const display_height{aspect == corncob::render::display_aspect::crt ? display_width * 3 / 4 : static_cast<int>(frame.height)};
  glfwSetErrorCallback([](int const code, char const *const message){
    std::cerr << "ERROR: GLFW " << code << ": " << message << std::endl;
  });
  if(!glfwInit()) return startup_failure("GLFW initialisation failed");
  boost::scope::scope_exit terminate_glfw{[]{
    glfwTerminate();
  }};
  if(arguments.contains("list-joysticks")) {
    bool found{false};
    for(int device{GLFW_JOYSTICK_1}; device <= GLFW_JOYSTICK_LAST; ++device) if(glfwJoystickPresent(device)) {
      std::cout << device << ": " << glfwGetJoystickName(device) << '\n';
      found = true;
    }
    if(!found) std::cout << "No joysticks connected" << std::endl;
    return EXIT_SUCCESS;
  }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  bool const compositor_sizing{automatic_scale && glfwGetPlatform() == GLFW_PLATFORM_WAYLAND};
  glfwWindowHint(GLFW_MAXIMIZED, compositor_sizing ? GLFW_TRUE : GLFW_FALSE);
  std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> const window{
    glfwCreateWindow(display_width * scale, display_height * scale,
                     "Corncob Redux", nullptr, nullptr),
    glfwDestroyWindow,
  };
  if(!window) return startup_failure("cannot create the Corncob window");
  if(automatic_scale && !compositor_sizing) {
    if(auto *monitor{glfwGetPrimaryMonitor()}) {
      int x{0}, y{0}, width{0}, height{0};
      glfwGetMonitorWorkarea(monitor, &x, &y, &width, &height);
      int left{0}, top{0}, right{0}, bottom{0};
      glfwGetWindowFrameSize(window.get(), &left, &top, &right, &bottom);
      if(width > left + right && height > top + bottom) {
        auto const fitted{corncob::render::fit_viewport(width - left - right, height - top - bottom, display_width, display_height)};
        glfwSetWindowSize(window.get(), fitted.width, fitted.height);
        glfwSetWindowPos(window.get(), x + left + fitted.x, y + top + fitted.y);
      }
    }
  }
  glfwShowWindow(window.get());
  if(compositor_sizing) {
    int width{0}, height{0};
    glfwGetWindowSize(window.get(), &width, &height);
    glfwRestoreWindow(window.get());
    double const deadline{glfwGetTime() + 1.0};
    while(glfwGetWindowAttrib(window.get(), GLFW_MAXIMIZED) && glfwGetTime() < deadline) glfwWaitEventsTimeout(0.01);
    auto const fitted{corncob::render::fit_viewport(width, height, display_width, display_height)};
    glfwSetWindowSize(window.get(), fitted.width, fitted.height);
  }
  glfwMakeContextCurrent(window.get());
  glfwSwapInterval(1);
  bool sound_enabled{!arguments.contains("no-sound")};
  window_state state{game, front, sound_enabled, frame};
  corncob::platform::window_mode display_mode{*window};
  glfwSetWindowUserPointer(window.get(), &state);
  glfwSetKeyCallback(window.get(), [](GLFWwindow *const window, int const key, int const /*scancode*/, int const action, int const modifiers){
    // media keys can be unknown; -1 is also our unassigned-binding sentinel
    if(key == GLFW_KEY_UNKNOWN) return;
    auto &state{*static_cast<window_state*>(glfwGetWindowUserPointer(window))};
    auto &game{state.game};
    auto &front{state.front};
    if(key >= 0 && action == GLFW_RELEASE && state.consumed_key_releases.test(static_cast<unsigned int>(key))) {
      state.consumed_key_releases.reset(static_cast<unsigned int>(key));
      return;
    }
    if((key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) && (modifiers & GLFW_MOD_ALT) != 0) {
      if(action == GLFW_PRESS) {
        front.set_fullscreen(!front.options().fullscreen);
        state.consumed_key_releases.set(static_cast<unsigned int>(key));
      }
      return;
    }
    if(front.capturing_binding()) {
      if(action == GLFW_PRESS) { front.bind_input(key); if(key >= 0) state.consumed_key_releases.set(static_cast<unsigned int>(key)); }
      return;
    }
    if(front.current_page() == corncob::game::interface_page::briefing && action != GLFW_RELEASE) {
      /// DOS consumes presses and repeats; a closing reader must not pass the matching release into flight
      using enum corncob::game::interface_command;
      if(key < 0 || (key >= GLFW_KEY_LEFT_SHIFT && key <= GLFW_KEY_RIGHT_SUPER)) return;
      state.consumed_key_releases.set(static_cast<unsigned int>(key));
      if((modifiers & GLFW_MOD_CONTROL) != 0 && key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
        front.character(static_cast<uint32_t>(key - GLFW_KEY_A + 1));
        return;
      }
      switch(key) {
      case GLFW_KEY_UP: case GLFW_KEY_LEFT: case GLFW_KEY_KP_4: case GLFW_KEY_KP_8: front.command(up); break;
      case GLFW_KEY_DOWN: case GLFW_KEY_RIGHT: case GLFW_KEY_KP_2: case GLFW_KEY_KP_6: front.command(down); break;
      case GLFW_KEY_HOME: case GLFW_KEY_KP_7: front.command(home); break;
      case GLFW_KEY_END: case GLFW_KEY_KP_1: front.command(end); break;
      case GLFW_KEY_PAGE_UP: case GLFW_KEY_KP_9: front.command(page_up); break;
      case GLFW_KEY_PAGE_DOWN: case GLFW_KEY_KP_3: front.command(page_down); break;
      case GLFW_KEY_ENTER: case GLFW_KEY_KP_ENTER: front.command(accept); break;
      case GLFW_KEY_ESCAPE: case GLFW_KEY_BACKSPACE: front.command(cancel); break;
      case GLFW_KEY_F1: front.show_boss(); break;
      default: front.command(any_key); break;
      }
      return;
    }
    if(action != GLFW_RELEASE) return;
    if(!front.in_flight()) {
      using enum corncob::game::interface_command;
      if(front.current_page() == corncob::game::interface_page::help && (key == GLFW_KEY_P || key == GLFW_KEY_ESCAPE)) {
        if(game->is_paused()) game->press(corncob::game::action::pause);
        front.command(cancel);
        return;
      }
      if(front.current_page() == corncob::game::interface_page::radio) {
        if(key == GLFW_KEY_O && game->messages_enabled()) game->press(corncob::game::action::toggle_messages);
        front.command(accept);
        return;
      }
      if(front.current_page() == corncob::game::interface_page::boss) {
        /// Modifier releases do not produce a DOS input character and must not dismiss the disguise
        if(key < GLFW_KEY_LEFT_SHIFT || key > GLFW_KEY_RIGHT_SUPER) front.command(accept);
        return;
      }
      switch(key) {
      case GLFW_KEY_UP: front.command(up); break;
      case GLFW_KEY_DOWN: front.command(down); break;
      case GLFW_KEY_LEFT: front.command(previous); break;
      case GLFW_KEY_RIGHT: front.command(next); break;
      case GLFW_KEY_INSERT: front.command(details); break;
      case GLFW_KEY_HOME: front.command(home); break;
      case GLFW_KEY_END: front.command(end); break;
      case GLFW_KEY_PAGE_UP: front.command(page_up); break;
      case GLFW_KEY_PAGE_DOWN: front.command(page_down); break;
      case GLFW_KEY_ENTER: case GLFW_KEY_KP_ENTER: front.command(accept); break;

      case GLFW_KEY_SPACE:
        if(!front.editing_name() && (front.current_page() == corncob::game::interface_page::help
            || front.current_page() == corncob::game::interface_page::title || front.current_page() == corncob::game::interface_page::story
            || front.current_page() == corncob::game::interface_page::high_scores || front.current_page() == corncob::game::interface_page::notice)) front.command(accept);
        break;
      case GLFW_KEY_BACKSPACE: front.command(backspace); break;
      case GLFW_KEY_ESCAPE: front.command(cancel); break;
      case GLFW_KEY_F4: front.command(help); break;
      default: break;
      }
      return;
    }
    if(game->in_recording() && key == GLFW_KEY_ESCAPE) { front.leave_sortie(); return; }
    if(game->in_builder()) {
      using enum corncob::game::builder_command;
      if(key >= GLFW_KEY_1 && key <= GLFW_KEY_8) game->builder_speed(static_cast<unsigned int>(key - GLFW_KEY_0));
      switch(key) {
      case GLFW_KEY_LEFT_SHIFT: game->edit(cursor); break;
      case GLFW_KEY_LEFT_CONTROL: game->edit(hide_cursor); break;
      case GLFW_KEY_C: game->edit(create); break;
      case GLFW_KEY_SPACE: game->edit(select); break;
      case GLFW_KEY_D: game->edit(erase); break;
      case GLFW_KEY_EQUAL: game->edit(next_type); break;
      case GLFW_KEY_MINUS: game->edit(previous_type); break;
      case GLFW_KEY_RIGHT_BRACKET: game->edit(next_quality); break;
      case GLFW_KEY_LEFT_BRACKET: game->edit(previous_quality); break;
      case GLFW_KEY_F9: game->edit(special); break;
      case GLFW_KEY_F10: game->edit(complete); break;
      case GLFW_KEY_CAPS_LOCK: game->edit(level); break;
      case GLFW_KEY_H: game->edit((modifiers & GLFW_MOD_ALT) != 0 ? remember_height : lock_height); break;
      case GLFW_KEY_B: case GLFW_KEY_M: case GLFW_KEY_ESCAPE: game->edit(map); break;
      case GLFW_KEY_F5: front.leave_sortie(); break;
      default: break;
      }
      return;
    }
    if(game->in_map()) {
      for(auto const &binding : front.options().bindings) if(binding.id == corncob::game::control_id::map && binding.matches_key(key)) { game->press(corncob::game::action::toggle_map); return; }
      if(key == GLFW_KEY_ESCAPE || key == GLFW_KEY_M || key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER || key == GLFW_KEY_SPACE) game->press(corncob::game::action::toggle_map);
      return;
    }
    if(key == GLFW_KEY_C && (modifiers & GLFW_MOD_CONTROL) != 0) { front.abort_sortie(); return; }
    if(key == GLFW_KEY_I && (modifiers & GLFW_MOD_ALT) != 0) { game->press(corncob::game::action::toggle_invulnerability); return; }
    if(key == GLFW_KEY_ESCAPE) { front.show_menu(); return; }
    if(key == GLFW_KEY_F1) { front.show_boss(); return; }
    if(key == GLFW_KEY_F2) { front.show_intelligence(game->world(), corncob::world::nearest_airfield(game->world(), game->camera().position)); return; }
    if(key == GLFW_KEY_F3) { front.show_current_score(game->results(), game->world().training); return; }
    if(key == GLFW_KEY_F4) { front.show_flight_help(); return; }
    bool const walking{game->player() == corncob::game::player_mode::on_foot && !game->remote_control()};
    for(auto const &binding : front.options().bindings) if(applies(binding, walking) && binding.matches_key(key)) activate_control(state, binding.id);
  });
  glfwSetMouseButtonCallback(window.get(), [](GLFWwindow *window, int button, int action, int){
    auto &state{*static_cast<window_state*>(glfwGetWindowUserPointer(window))};
    if(state.front.capturing_binding()) { if(action == GLFW_PRESS) state.front.bind_input(-1, button); return; }
    if(!state.front.in_flight() || action != GLFW_RELEASE || !glfwGetWindowAttrib(window, GLFW_FOCUSED)) return;
    bool const walking{state.game->player() == corncob::game::player_mode::on_foot && !state.game->remote_control()};
    for(auto const &binding : state.front.options().bindings) if(applies(binding, walking) && binding.mouse == button) activate_control(state, binding.id);
  });
  glfwSetScrollCallback(window.get(), [](GLFWwindow *window, double, double y){
    auto &state{*static_cast<window_state*>(glfwGetWindowUserPointer(window))};
    state.wheel_remainder -= y;
    int const steps{static_cast<int>(state.wheel_remainder)};
    state.wheel_remainder -= steps;
    state.front.wheel(steps);
  });
  glfwSetCursorPosCallback(window.get(), [](GLFWwindow *window, double x, double y){
    auto &state{*static_cast<window_state*>(glfwGetWindowUserPointer(window))};
    if(!state.mouse_captured || !glfwGetWindowAttrib(window, GLFW_FOCUSED)) { state.mouse_primed = false; return; }
    if(state.mouse_primed) { state.look_x += x - state.last_x; state.look_y += y - state.last_y; }
    state.last_x = x; state.last_y = y; state.mouse_primed = true;
  });
  glfwSetWindowFocusCallback(window.get(), [](GLFWwindow *window, int focused){
    auto &state{*static_cast<window_state*>(glfwGetWindowUserPointer(window))};
    if(!focused) {
      state.look_x = state.look_y = 0;
      state.mouse_primed = state.mouse_captured = false;
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
      if(state.front.in_flight() && !state.changing_window_mode) state.front.show_menu();
    }
  });
  glfwSetCharCallback(window.get(), [](GLFWwindow *const window, unsigned int const codepoint){
    auto &state{*static_cast<window_state*>(glfwGetWindowUserPointer(window))};
    /// Keypad navigation is handled once on key release, irrespective of Num Lock's optional character event
    if(state.front.current_page() == corncob::game::interface_page::briefing) {
      for(int const key : {GLFW_KEY_KP_2, GLFW_KEY_KP_4, GLFW_KEY_KP_6, GLFW_KEY_KP_8}) {
        if(glfwGetKey(window, key) == GLFW_PRESS) return;
      }
    }
    state.front.character(codepoint);
  });
  corncob::resources::collection expansion_assets{corncob::resources::other_worlds_asset(), &interface_assets};
  corncob::audio::effects sound{campaign->current_campaign() == corncob::world::campaign_id::earth ? interface_assets : expansion_assets};
  corncob::audio::music music;
  struct audio_mix {
    corncob::audio::effects &effects;
    corncob::audio::music &music;
  } mix{sound, music};
  bool audio_unavailable{false};
  std::unique_ptr<corncob::platform::audio_output> audio_output;
  corncob::platform::framebuffer_presenter presenter{*window, aspect};
  corncob::render::framebuffer flight_frame;
  bool flight_frame_valid{false};
  unsigned int frame_number{0};
  double previous_time{glfwGetTime()};
  bool clock_was_flying{front.in_flight()};
  while(!glfwWindowShouldClose(window.get())) {
    glfwPollEvents();
    display_mode.remember_windowed();
    if(preferences.fullscreen != display_mode.fullscreen()) {
      state.changing_window_mode = true;
      bool const changed{display_mode.set_fullscreen(preferences.fullscreen)};
      state.changing_window_mode = false;
      state.look_x = state.look_y = 0; state.mouse_primed = false;
      game->suspend_clock();
      if(!changed) front.set_fullscreen(display_mode.fullscreen());
    }
    sound.volume(static_cast<float>(preferences.effects_volume));
    music.volume(static_cast<float>(preferences.music_volume));
    if(clock_was_flying && !front.in_flight()) game->suspend_clock();
    clock_was_flying = front.in_flight();
    if(front.quitting()) break;
    if(!audio_unavailable && (sound_enabled || front.music() != corncob::audio::music_cue::silent) && !audio_output) {
      try {
        audio_output = std::make_unique<corncob::platform::audio_output>([](void *const userdata, std::span<float> const samples) noexcept {
          auto &mix{*static_cast<audio_mix*>(userdata)};
          mix.effects.render(samples);
          mix.music.mix(samples);
        }, &mix);
      } catch(std::exception const &error) {
        std::cerr << "WARNING: audio unavailable: " << error.what() << std::endl;
        sound_enabled = false;
        audio_unavailable = true;
      }
    }
    if(auto const launch{front.take_launch()}) {
      sound.reset(campaign->current_campaign() == corncob::world::campaign_id::earth ? interface_assets : expansion_assets);
      theatre_name = launch->theatre;
      reserve_aircraft = launch->reserve_aircraft;
      editing_session = launch->editing;
      demonstration_session = !launch->recording.empty();
      auto world{launch->training ? corncob::world::generate_training(geometry, *launch->training) : corncob::world::load_theatre(theatre_name)};
      if(auto saved{launch->training || demonstration_session || editing_session ? std::nullopt : campaign->load_world(theatre_name)}) {
        saved->sky = world.sky;
        saved->environment = world.environment;
        saved->briefing = std::move(world.briefing);
        saved->missions = std::move(world.missions);
        world = std::move(*saved);
      }
      world.expansion = campaign->current_campaign() == corncob::world::campaign_id::other_worlds;
      corncob::world::select_airfield(world, launch->airfield);
      game = std::make_unique<corncob::game::session>(geometry, std::move(world), reserve_aircraft);
      game->set_experimental_clearance(game->world().expansion || (campaign->active_pilot() && campaign->active_pilot()->awards(corncob::game::medal::combat_master) != 0));
      renderer = std::make_unique<corncob::graphics::scene_renderer>(geometry, cockpit, video);
      renderer->configure(preferences.scene);
      renderer->initialise(game->camera().position, game->random_stream());
      game->set_invincible(launch->invincible);
      if(launch->editing) game->begin_builder();
      if(demonstration_session) game->begin_recording(expansion_assets.file("dmo/" + launch->recording + ".dmo"));
      if(!launch->comments) game->press(corncob::game::action::toggle_messages);
      game->set_beginner_flight(front.flight_controls().beginner_flight);
      front.begin_flight();
      // A menu shortcut can launch on its press; its later release belongs to that menu, not the new flight/editor.
      for(int key{GLFW_KEY_SPACE}; key <= GLFW_KEY_LAST; ++key) {
        if(glfwGetKey(window.get(), key) == GLFW_PRESS) state.consumed_key_releases.set(static_cast<unsigned int>(key));
      }
      observer = game->camera();
      flight_frame_valid = false;
      clock_was_flying = true;
    }
    if(front.in_flight()) game->resume_from_tower();
    if(front.take_map()) game->press(corncob::game::action::toggle_map);
    if(auto const relocation{front.take_relocation()}; relocation && game->player() == corncob::game::player_mode::in_aircraft) game->relocate(*relocation);
    double const now{glfwGetTime()};
    double const elapsed{std::min(now - previous_time, 0.1)};
    previous_time = now;
    front.advance(elapsed);
#ifndef NDEBUG
    if(seconds > 0.0 && now >= seconds) break;
    if(free_camera && glfwGetWindowAttrib(window.get(), GLFW_FOCUSED)) {
      auto held{[&window](int const key){
        return glfwGetKey(window.get(), key) == GLFW_PRESS;
      }};
      int const turn{static_cast<int>(elapsed * 10'000.0)};
      observer.attitude.heading += static_cast<corncob::maths::angle>((held(GLFW_KEY_LEFT) - held(GLFW_KEY_RIGHT)) * turn);
      observer.attitude.pitch += static_cast<corncob::maths::angle>((held(GLFW_KEY_DOWN) - held(GLFW_KEY_UP)) * turn);
      auto const axes{corncob::maths::make_rotation(observer.attitude)};
      double const speed{elapsed * (held(GLFW_KEY_LEFT_SHIFT) ? 200'000.0 : 25'000.0)};
      for(unsigned int axis{0}; axis != 3; ++axis) {
        double const movement{((held(GLFW_KEY_W) - held(GLFW_KEY_S)) * axes.rows[0][axis]
          + (held(GLFW_KEY_A) - held(GLFW_KEY_D)) * axes.rows[1][axis]) * speed / 32'768.0};
        observer.position[axis] += static_cast<int32_t>(movement);
      }
      observer.position.z += static_cast<int32_t>((held(GLFW_KEY_PAGE_UP) - held(GLFW_KEY_PAGE_DOWN)) * speed);
      if(held(GLFW_KEY_HOME)) {
        observer.position = corncob::maths::world_position{game->world().start};
        observer.attitude = {
          .heading{static_cast<corncob::maths::angle>(-8000)},
          .pitch{static_cast<corncob::maths::angle>(-1500)},
          .roll{0},
        };
      }
    }
#endif // NDEBUG
    corncob::game::input_state input;
    bool fast_forwarding{false};
    auto configure_renderer{[&]{
      auto settings{preferences.scene};
      if(preferences.window_resolution) {
        int width{}, height{}; glfwGetFramebufferSize(window.get(), &width, &height);
        settings.resolution = {static_cast<unsigned int>(std::clamp(width, static_cast<int>(resolution_limits::minimum_width), static_cast<int>(resolution_limits::maximum_width))), static_cast<unsigned int>(std::clamp(height, static_cast<int>(resolution_limits::minimum_height), static_cast<int>(resolution_limits::maximum_height)))};
      }
      renderer->configure(settings);
    }};
    configure_renderer();
    bool const capture{!inspect && front.in_flight() && !game->in_builder() && !game->in_map() && game->player() == corncob::game::player_mode::on_foot && !game->remote_control() && glfwGetWindowAttrib(window.get(), GLFW_FOCUSED)};
    if(capture != state.mouse_captured) {
      state.mouse_captured = capture; state.mouse_primed = false;
      state.look_x = state.look_y = 0;
      glfwSetInputMode(window.get(), GLFW_CURSOR, capture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
      if(glfwRawMouseMotionSupported()) glfwSetInputMode(window.get(), GLFW_RAW_MOUSE_MOTION, capture ? GLFW_TRUE : GLFW_FALSE);
    }
    game->set_beginner_flight(front.flight_controls().beginner_flight);
    auto draw_flight{[&](bool const preview){
      observer = game->render_camera();
#ifndef NDEBUG
      if(no_cockpit) observer.cockpit = false;
#endif // NDEBUG
      corncob::graphics::instrument_telemetry const telemetry{
        .aircraft{game->craft()},
        .controls{input.stick},
        .paused{game->is_paused()},
        .completed{game->statistics().objectives_completed != 0},
        .autopilot{game->autopilot_engaged()},
        .low_altitude{game->low_altitude_warning()},
        .timer{static_cast<uint16_t>(game->timer())},
        .bombs{game->bomb_supply()},
        .rocket_colour{game->rocket_indicator_colour()},
        .damage{&game->aircraft_damage()},
        .slalom{slalom_mode},
        .slalom_progress{game->slalom_progress()},
      };
      auto const *const panel{!game->in_map() && game->player() == corncob::game::player_mode::in_aircraft ? &telemetry : nullptr};
      if(preview) renderer->preview(game->world(), observer, game->frame_number(), static_cast<uint16_t>(game->timer()), game->random_stream(), flight_frame, panel);
      else renderer->render(game->world(), observer, game->frame_number(), static_cast<uint16_t>(game->timer()), game->random_stream(), flight_frame, panel);
      if(game->in_builder()) {
        corncob::render::framebuffer builder_overlay;
        corncob::graphics::interface_canvas overlay{builder_overlay, interface_assets.file("font8.bin"), 8};
        overlay.rectangle(0, 0, 640, 34, {0, 0, 0});
        overlay.text(4, 2, "MISSION BUILDER  Shift: cursor/commit  Ctrl: hide  C: create  Space: select", {255, 255, 85});
        overlay.text(4, 10, "Alt+arrows/PgUp/PgDn: move  A+arrows: rotate  +/-: type  []: quality  D: delete", {255, 255, 85});
        overlay.text(4, 18, "F9: special  F10: complete  H/Alt+H: height  B: map  F5: save and return", {255, 255, 85});
        overlay.text(4, 26, game->message(), {255, 255, 255});
        double const ui_scale{flight_frame.height / 350.0};
        for(unsigned int y{0}; y < std::min(flight_frame.height, static_cast<unsigned int>(34 * ui_scale)); ++y)
          for(unsigned int x{0}; x < flight_frame.width; ++x) {
            unsigned int const source_x{static_cast<unsigned int>(x / ui_scale)}, source_y{static_cast<unsigned int>(y / ui_scale)};
            flight_frame.pixels[y * flight_frame.width + x] = source_x < 640 ? builder_overlay.pixels[source_y * 640 + source_x] : corncob::render::rgba_pixel{0, 0, 0};
          }
      }
      flight_frame_valid = true;
      frame = flight_frame;
    }};
    auto update_audio{[&]{
      music.select(audio_unavailable || inspect ? corncob::audio::music_cue::silent : front.music());
      auto audio_frame{corncob::audio::observe(*game)};
      audio_frame.active = sound_enabled && !game->in_builder() && front.in_flight() && !game->is_paused() && !game->in_map() && !inspect;
      audio_frame.dead = audio_frame.dead && sound_enabled
        && (front.in_flight() || front.current_page() == corncob::game::interface_page::outcome);
      sound.update(audio_frame);
    }};
    if(!inspect && front.in_flight()) {
      if(glfwGetWindowAttrib(window.get(), GLFW_FOCUSED)) {
        auto held{[&window](int const key){ return glfwGetKey(window.get(), key) == GLFW_PRESS; }};
        bool const walking{game->player() == corncob::game::player_mode::on_foot && !game->remote_control()};
        auto down{[&](corncob::game::control_id const id){
          auto const found{std::ranges::find(preferences.bindings, id, &corncob::game::control_binding::id)};
          if(found == preferences.bindings.end() || !applies(*found, walking)) return false;
          return (found->key >= 0 && held(found->key)) || (found->alternate >= 0 && held(found->alternate)) || (found->mouse >= 0 && glfwGetMouseButton(window.get(), found->mouse) == GLFW_PRESS);
        }};
        using enum corncob::game::control_id;
        fast_forwarding = down(fast_forward) && !game->in_builder() && !game->in_map() && !game->is_paused();
        input.accelerated_walking = walking && fast_forwarding;
        input.stick.roll = static_cast<int16_t>((down(roll_right) - down(roll_left)) * 512);
        input.stick.pitch = static_cast<int16_t>((down(pitch_up) - down(pitch_down)) * 512);
        input.stick.rudder = static_cast<int16_t>((down(rudder_left) - down(rudder_right)) * 512);
        input.stick.brakes = down(brakes);
        input.throttle_up = down(throttle_up); input.throttle_down = down(throttle_down);
        input.walk_forward = down(forward); input.walk_backward = down(backward);
        input.strafe = static_cast<int16_t>(down(strafe_left) - down(strafe_right));
        input.jump = down(jump); input.drop_through = down(drop_through);
        input.weapons.gun = down(gun) || down(pistol); input.weapons.cannon = down(cannon); input.weapons.missile = down(missile);
        input.view = {.left{held(GLFW_KEY_LEFT)}, .right{held(GLFW_KEY_RIGHT)}, .up{held(GLFW_KEY_UP)}, .down{held(GLFW_KEY_DOWN)}, .below{held(GLFW_KEY_KP_5)}, .modifier{held(GLFW_KEY_LEFT_CONTROL) || held(GLFW_KEY_RIGHT_CONTROL)}};
        input.map = {.higher{held(GLFW_KEY_PAGE_DOWN) || held(GLFW_KEY_KP_3)}, .lower{held(GLFW_KEY_PAGE_UP) || held(GLFW_KEY_KP_9)}, .home{held(GLFW_KEY_HOME)}, .rotate{held(GLFW_KEY_LEFT_SHIFT)}, .slow{held(GLFW_KEY_S)}, .move_object{held(GLFW_KEY_LEFT_ALT)}, .rotate_object{held(GLFW_KEY_A)}};
        if(walking) {
          input.look_yaw = static_cast<int16_t>(std::clamp(-state.look_x * preferences.mouse_sensitivity, -16000.0, 16000.0));
          input.look_pitch = static_cast<int16_t>(std::clamp(state.look_y * preferences.mouse_sensitivity * (preferences.invert_mouse ? -1 : 1), -16000.0, 16000.0));
        }
        if(game->in_builder() || game->in_map()) {
          input.stick.roll = static_cast<int16_t>((held(GLFW_KEY_RIGHT) - held(GLFW_KEY_LEFT)) * 512);
          input.stick.pitch = static_cast<int16_t>((held(GLFW_KEY_DOWN) - held(GLFW_KEY_UP)) * 512);
          input.walk_forward = held(GLFW_KEY_KP_5); input.walk_backward = held(GLFW_KEY_KP_0);
        }
        if(auto const joystick{walking ? std::nullopt : corncob::platform::poll_joystick(front.flight_controls().joystick_device)})
          corncob::game::apply_joystick(input, *joystick, input.view.modifier || game->looking_around());
      }
      double const game_seconds{game->update_frame(now, input, fast_forwarding)};
      if(game_seconds > 0) {
        input.look_yaw = input.look_pitch = 0;
        state.look_x = state.look_y = 0;
      }
      observer = game->camera();
#ifndef NDEBUG
      if(no_cockpit) observer.cockpit = false;
#endif // NDEBUG
      if(frame_number % 15 == 0) {
        std::string const title{"Corncob Redux - " + game->message()};
        glfwSetWindowTitle(window.get(), title.c_str());
      }
    }
    update_audio();
    if(!inspect && (front.take_finish() || (front.in_flight() && game->finished()))) {
      if(slalom_mode) break; // Source experiments never write into a named pilot's campaign.
      auto const result{game->results(front.take_abort())};
      auto const previous{campaign->active_pilot() ? std::optional{*campaign->active_pilot()} : std::nullopt};
      auto const active_theatre{campaign->active_theatre()};
      auto const theatres{campaign->theatres()};
      auto const previous_theatre{theatre_name != "training" && active_theatre && *active_theatre < theatres.size() ? std::optional{theatres[*active_theatre].pilot} : std::nullopt};
      if(!demonstration_session) campaign->finish_sortie(theatre_name, game->snapshot(), result, reserve_aircraft);
      if(demonstration_session) {
        front.finish_editing();
        demonstration_session = false;
      } else if(editing_session) {
        auto const path{campaign->save_directory() / "custom_worlds" / (std::string{corncob::world::find_theatre(theatre_name).dos_name}
          + "_" + std::to_string(active_theatre && *active_theatre < theatres.size() ? theatres[*active_theatre].number : 0) + ".def")};
        auto exported{game->snapshot()};
        exported.expansion = true;
        corncob::platform::write_binary(path, corncob::world::encode_theatre(exported));
        auto briefing_path{path};
        briefing_path.replace_extension(".twr");
        corncob::platform::write_binary(briefing_path, corncob::world::theatre_briefing(theatre_name));
        front.finish_editing();
        editing_session = false;
      } else front.finish_sortie(result, previous ? &*previous : nullptr, previous_theatre ? &*previous_theatre : nullptr);
      if(result.has(corncob::game::sortie_status::earth_saved)) front.begin_ending(renderer->indexed_frame(), renderer->colours());
    }
    if(front.in_flight() && !inspect && game->take_tower()) front.show_intelligence(game->world(), corncob::world::nearest_airfield(game->world(), game->camera().position));
    if(front.in_flight() && !inspect) if(auto const notice{game->take_radio()}) {
      if(!flight_frame_valid) draw_flight(true);
      front.show_radio(*notice, flight_frame);
    }
    if(!front.in_flight() && !inspect) {
      if(clock_was_flying) game->suspend_clock();
      clock_was_flying = false;
      front.render(frame);
      presenter.present(frame);
      ++frame_number;
      glfwWaitEventsTimeout(1.0 / 60.0);
      continue;
    }
#ifndef NDEBUG
    if(inspect) {
      if(!cockpit_preview) renderer->preview(game->world(), observer, frame_number,
        static_cast<uint16_t>(static_cast<uint64_t>(now * 1'193'182.0 / 1024.0)), game->random_stream(), frame);
    } else
#endif // NDEBUG
    {
      draw_flight(game->is_paused());
      frame = flight_frame;
    }
    ++frame_number;
    presenter.present(frame);
    glfwWaitEventsTimeout(1.0 / 60.0);
  }
  return EXIT_SUCCESS;
}

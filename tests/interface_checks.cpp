#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include "game/front_end.h"
#include "render/video_mode.h"
#include "resources/assets.h"

namespace {

void capture(corncob::render::framebuffer const &frame, std::filesystem::path const &path) {
  /// Optional development captures retain each original video mode's actual pixel dimensions
  std::ofstream file{path, std::ios::binary};
  file.exceptions(std::ios::badbit | std::ios::failbit);
  file << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
  for(unsigned int i{0}; i != frame.width * frame.height; ++i) {
    auto const &pixel{frame.pixels[i]};
    file.put(static_cast<char>(pixel.red));
    file.put(static_cast<char>(pixel.green));
    file.put(static_cast<char>(pixel.blue));
  }
}

} // anonymous namespace

int main(int const argc, char const *const argv[]) {
  /// Decode every original composition and traverse title, briefing, help, confirmation and outcome chains without a window
  namespace game = corncob::game;
  corncob::resources::collection assets{corncob::resources::front_end_asset()};
  auto const stories{assets.story()}, outcomes{assets.outcomes()};
  if(stories.size() != 2 || outcomes.size() != 15) throw std::runtime_error{"original presentation count changed"};
  corncob::render::framebuffer frame;
  if(argc < 2 || argc > 3) throw std::invalid_argument{"interface_checks requires its dedicated scratch directory and optional capture directory"};
  std::filesystem::path const scratch{argv[1]};
  if(scratch.filename() != "interface-checks") throw std::invalid_argument{"interface check directory must be named interface-checks"};
  std::filesystem::remove_all(scratch);
  game::campaign_store career{scratch};
  career.keyboard_control(true);
  game::front_end front{assets, career};
  front.advance(0.4);
  front.render(frame);
  if(frame.width != 320 || frame.height != 200) throw std::runtime_error{"title lost its original VGA resolution"};
  if(argc == 3) std::filesystem::create_directories(argv[2]);
  if(argc == 3) capture(frame, std::filesystem::path{argv[2]} / "title.ppm");
  for(unsigned int i{0}; i != stories.size(); ++i) {
    corncob::graphics::draw_presentation(frame, assets, stories[i]);
    if(argc == 3) capture(frame, std::filesystem::path{argv[2]} / ("story" + std::to_string(i) + ".ppm"));
  }
  for(unsigned int i{0}; i != outcomes.size(); ++i) {
    front.show_outcome(i);
    front.render(frame);
    if(argc == 3) capture(frame, std::filesystem::path{argv[2]} / ("outcome" + std::to_string(i) + ".ppm"));
  }
  front.command(game::interface_command::accept);
  front.render(frame);
  if(frame.width != 640 || frame.height != 400) throw std::runtime_error{"menu lost its EGA text resolution"};
  if(argc == 3) capture(frame, std::filesystem::path{argv[2]} / "menu.ppm");
  auto save_capture{[&](std::string_view const name){
    front.render(frame);
    if(argc == 3) capture(frame, std::filesystem::path{argv[2]} / (std::string{name} + ".ppm"));
  }};
  front.character('d');
  save_capture("roster-empty");
  front.character('c');
  for(unsigned int c : std::string{"Test Pilot"}) front.character(c);
  save_capture("create-name");
  front.command(game::interface_command::accept);
  if(career.active_pilot()) throw std::runtime_error{"pilot creation skipped the original activation question"};
  front.command(game::interface_command::accept);
  if(!career.active_pilot() || career.active_pilot()->name() != "Test Pilot") throw std::runtime_error{"roster name entry failed"};
  save_capture("roster");
  front.character('v');
  if(front.current_page() != game::interface_page::records) throw std::runtime_error{"roster records were not reachable"};
  save_capture("records");
  front.command(game::interface_command::details);
  save_capture("record-statistics");
  auto const menu_font{assets.menu_font()};
  auto const medals{assets.file("medal.fnc")};
  for(unsigned int offset{0}; offset != medals.size(); offset += 17)
    for(unsigned int row{0}; row != 16; ++row)
      if(menu_font[medals[offset + 16] * 16u + row] != medals[offset + row]) throw std::runtime_error{"medal glyph differs from its original font record"};
  front.command(game::interface_command::cancel);
  front.command(game::interface_command::cancel);
  front.character('t');
  save_capture("training");
  front.character('w');
  save_capture("training-quality");
  front.character('m');
  front.character('8');
  front.command(game::interface_command::accept);
  front.command(game::interface_command::cancel);
  front.character('n');
  save_capture("training-counts");
  front.character('a');
  front.character('2'); front.character('6');
  front.command(game::interface_command::accept);
  if(front.current_page() != game::interface_page::training_value) throw std::runtime_error{"training accepted more than 25 sites"};
  front.command(game::interface_command::backspace);
  front.command(game::interface_command::backspace);
  front.character('2'); front.character('5');
  front.command(game::interface_command::accept);
  if(career.training_settings().aaa.count != 25 || career.training_settings().kla.quality != 7) throw std::runtime_error{"nested training editors lost their settings"};
  front.command(game::interface_command::cancel);
  front.character('d');
  if(career.training_settings().aaa.count != 6) throw std::runtime_error{"training defaults were not restored"};
  front.character('i');
  save_capture("training-information");
  front.command(game::interface_command::page_down);
  front.command(game::interface_command::cancel);
  if(front.current_page() != game::interface_page::training) throw std::runtime_error{"menu information returned to the wrong screen"};
  front.command(game::interface_command::cancel);
  front.character('h');
  save_capture("score-menu");
  for(unsigned int c : std::string{"scf"}) {
    front.character(c);
    save_capture(c == 's' ? "high-scores" : c == 'c' ? "top-careers" : "fast-careers");
    front.command(game::interface_command::cancel);
  }
  front.character('d'); front.character('s');
  front.command(game::interface_command::cancel);
  if(front.current_page() != game::interface_page::score_delete) throw std::runtime_error{"score deletion did not cancel"};
  front.command(game::interface_command::cancel);
  front.command(game::interface_command::cancel);
  front.character('v');
  save_capture("documentation");
  front.character('1'); front.character('7');
  front.command(game::interface_command::accept);
  front.command(game::interface_command::end);
  front.command(game::interface_command::cancel);
  front.command(game::interface_command::cancel);
  front.character('r');
  save_capture("read-this");
  front.command(game::interface_command::cancel);
  front.character('c');
  save_capture("combat-empty");
  front.character('o');
  save_capture("new-theatre");
  front.command(game::interface_command::accept);
  if(!career.active_theatre() || career.theatres().size() != 1 || !career.load_world("morning")) throw std::runtime_error{"opening a theater did not create its saved world"};
  front.character('o'); front.command(game::interface_command::down); front.command(game::interface_command::accept);
  if(career.theatres().size() != 2 || career.active_theatre() != 1) throw std::runtime_error{"opening another theater lost the first"};
  front.character('a'); front.command(game::interface_command::accept);
  if(career.active_theatre() != 0) throw std::runtime_error{"theater activation lost its saved-world index"};
  save_capture("combat");
  front.character('v'); front.command(game::interface_command::accept);
  save_capture("theatre-records");
  front.command(game::interface_command::cancel);
  front.character('f');
  if(front.take_launch() || front.current_page() != game::interface_page::starting_airfield) throw std::runtime_error{"combat skipped its starting-airfield screen"};
  front.character('i');
  if(front.current_page() != game::interface_page::airfield_information) throw std::runtime_error{"starting-airfield information was not reachable"};
  front.command(game::interface_command::page_down);
  save_capture("airfield-information");
  front.command(game::interface_command::cancel);
  front.command(game::interface_command::accept);
  auto const launch{front.take_launch()};
  if(!launch || launch->theatre != "morning" || front.take_launch()) throw std::runtime_error{"menu launch request lost or repeated"};
  front.begin_flight();
  if(front.music() != corncob::audio::music_cue::silent) throw std::runtime_error{"menu music continued into a sortie"};
  front.show_flight_help();
  if(front.music() != corncob::audio::music_cue::silent) throw std::runtime_error{"flight helper enabled menu music"};
  if(front.current_page() != game::interface_page::bindings) throw std::runtime_error{"F4 did not open current control bindings"};
  front.render(frame);
  front.command(game::interface_command::cancel);
  if(!front.in_flight()) throw std::runtime_error{"control menu did not resume the current sortie"};
  for(unsigned int notice{0}; notice != static_cast<unsigned int>(std::to_underlying(corncob::resources::radio_notice::count)); ++notice) {
    auto const type{static_cast<corncob::resources::radio_notice>(notice)};
    auto const message{corncob::resources::read_radio_message(type)};
    if(message.lines.empty() || message.border.left < 0 || message.border.left + message.border.width > 640) throw std::runtime_error{"original radio message has invalid placement"};
    front.show_radio(type, frame, type == corncob::resources::radio_notice::map_instructions);
    front.render(frame);
    if(argc == 3) capture(frame, std::filesystem::path{argv[2]} / ("radio" + std::to_string(notice) + ".ppm"));
    front.command(game::interface_command::accept);
    if(!front.in_flight() || front.take_map() != (type == corncob::resources::radio_notice::map_instructions)) throw std::runtime_error{"radio acknowledgement lost flight/map continuation"};
  }
  // Warnings stay in the flight mode and leave every backdrop pixel outside their message box unchanged.
  for(auto const video : {corncob::render::video_mode::ega, corncob::render::video_mode::ega_low, corncob::render::video_mode::hercules}) {
    auto const size{corncob::render::dimensions(video)};
    corncob::render::framebuffer backdrop;
    backdrop.width = size.width;
    backdrop.height = size.height;
    backdrop.pixels.resize(size.width * size.height);
    for(unsigned int y{0}; y != size.height; ++y) for(unsigned int x{0}; x != size.width; ++x) backdrop.pixels[y * size.width + x] = {
      .red{static_cast<uint8_t>(x)},
      .green{static_cast<uint8_t>(y)},
      .blue{static_cast<uint8_t>(x + y)},
    };
    auto const viewport{corncob::render::fit_viewport(1280, 960, static_cast<int>(size.width), static_cast<int>(size.height))};
    for(unsigned int notice{0}; notice != static_cast<unsigned int>(std::to_underlying(corncob::resources::radio_notice::count)); ++notice) {
      auto const type{static_cast<corncob::resources::radio_notice>(notice)};
      auto const box{corncob::resources::read_radio_message(type).border};
      front.show_radio(type, backdrop);
      front.render(frame);
      if(frame.width != size.width || frame.height != size.height || frame.pixels.size() != backdrop.pixels.size())
        throw std::runtime_error{"radio message changed the flight display dimensions"};
      auto const displayed{corncob::render::fit_viewport(1280, 960, static_cast<int>(frame.width), static_cast<int>(frame.height))};
      if(displayed.width != viewport.width || displayed.height != viewport.height) throw std::runtime_error{"radio message changed the displayed enlargement"};
      unsigned int text_pixels{0};
      for(unsigned int y{0}; y != size.height; ++y) for(unsigned int x{0}; x != size.width; ++x) {
        int const message_x{static_cast<int>(x * 640 / size.width)}, message_y{static_cast<int>(y * 350 / size.height)};
        auto const &actual{frame.pixels[y * size.width + x]}, &expected{backdrop.pixels[y * size.width + x]};
        if(message_x >= box.left && message_x < box.left + box.width && message_y >= box.top && message_y < box.top + box.height) {
          if(actual.red == 170 && actual.green == 170 && actual.blue == 170) ++text_pixels;
        } else if(actual.red != expected.red || actual.green != expected.green || actual.blue != expected.blue)
          throw std::runtime_error{"radio overlay moved or resampled the surrounding scene"};
      }
      if(text_pixels < 20) throw std::runtime_error{"adapted radio text disappeared"};
      front.command(game::interface_command::accept);
    }
  }
  front.ask_to_end_sortie();
  front.command(game::interface_command::cancel);
  if(!front.in_flight()) throw std::runtime_error{"quit cancellation lost the flight"};
  front.ask_to_end_sortie();
  front.command(game::interface_command::up);
  front.command(game::interface_command::accept);
  if(!front.take_finish() || front.take_finish()) throw std::runtime_error{"end-sortie request lost or repeated"};
  front.abort_sortie();
  if(!front.take_finish() || !front.take_abort() || front.take_abort()) throw std::runtime_error{"abort status was lost or repeated"};
  auto const startup_muted{std::make_unique<game::front_end>(assets, career, true, false, std::nullopt, false)};
  if(startup_muted->music() != corncob::audio::music_cue::silent || !career.music_enabled()) throw std::runtime_error{"startup music override changed the saved preference"};
  auto const setup_storage{std::make_unique<game::front_end>(assets, career, true)};
  auto &setup{*setup_storage};
  setup.character('o');
  setup.command(game::interface_command::down);
  setup.command(game::interface_command::accept);
  if(setup.flight_controls().joystick_device != -1) throw std::runtime_error{"joystick menu did not select the joystick"};
  setup.command(game::interface_command::accept);
  setup.render(frame);
  if(argc == 3) capture(frame, std::filesystem::path{argv[2]} / "control-setup.ppm");
  setup.command(game::interface_command::cancel);
  setup.character('c'); setup.character('m'); setup.character('p'); setup.character('c');
  if(setup.flight_controls().beginner_flight || !career.combat_invincible() || career.inflight_comments()) throw std::runtime_error{"combat menu toggles did not change their independent settings"};
  setup.command(game::interface_command::cancel);
  setup.character('m');
  if(setup.music() != corncob::audio::music_cue::silent) throw std::runtime_error{"main-menu music toggle did not stop music"};
  game::campaign_store reopened{scratch};
  auto const restored_storage{std::make_unique<game::front_end>(assets, reopened, true)};
  auto &restored{*restored_storage};
  if(restored.music() != corncob::audio::music_cue::silent || restored.flight_controls().joystick_device != -2
      || restored.flight_controls().beginner_flight || !reopened.combat_invincible() || reopened.inflight_comments()) throw std::runtime_error{"original menu preferences were lost on restart"};
  restored.character('d'); restored.character('l'); restored.command(game::interface_command::accept);
  restored.render(frame);
  if(argc == 3) capture(frame, std::filesystem::path{argv[2]} / "delete-pilot.ppm");
  if(restored.current_page() != game::interface_page::pilot_delete) throw std::runtime_error{"pilot deletion confirmation was not reachable"};
  restored.command(game::interface_command::cancel);
  if(reopened.pilots().size() != 1) throw std::runtime_error{"cancelled pilot deletion removed the pilot"};
  restored.character('l'); restored.command(game::interface_command::accept); restored.command(game::interface_command::accept);
  if(!reopened.pilots().empty()) throw std::runtime_error{"confirmed pilot deletion did not remove the pilot"};
  restored.command(game::interface_command::cancel);
  restored.character('x');
  if(!restored.quitting()) throw std::runtime_error{"original exit shortcut did not quit immediately"};
  game::front_end intro{assets, reopened};
  for(unsigned int i{0}; i != 2; ++i) {
    intro.advance(0.4);
    intro.command(game::interface_command::accept);
    intro.advance(0.4);
  }
  if(intro.current_page() != game::interface_page::title) throw std::runtime_error{"story fades did not lead to the animated title"};
  intro.advance(4.0);
  intro.render(frame);
  if(argc == 3) capture(frame, std::filesystem::path{argv[2]} / "animated-title.ppm");
  intro.command(game::interface_command::accept);
  if(intro.current_page() != game::interface_page::menu) throw std::runtime_error{"animated title could not continue to the menu"};
  std::filesystem::remove_all(scratch);
  std::cout << "Two story panels, fifteen outcomes, ten help pages and native menu navigation exercised" << std::endl;
  return EXIT_SUCCESS;
}

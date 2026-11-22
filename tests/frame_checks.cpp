#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include "game/frame_clock.h"
#include "game/session.h"
#include "graphics/scene.h"
#include "resources/assets.h"
#include "tests/reference_reader.h"

int main(int const argc, char const *const argv[]) {
  if(argc != 2) throw std::invalid_argument{"native frame-clock fixture required"};
  /// Host refresh rate and paused inspection must not change a sortie's random continuation or rendered simulation frames
  namespace game = corncob::game;
  namespace graphics = corncob::graphics;
  namespace resources = corncob::resources;
  namespace world = corncob::world;
  corncob::tests::reference_reader input{argv[1], "CORNFRM1"};
  auto const cases{input.dword()};
  for(unsigned int i{0}; i != cases; ++i) {
    auto const current{input.word()}, expected{input.word()};
    if(game::advance_frame(current) != expected) throw std::runtime_error{"native animation-frame restart differs"};
  }
  resources::geometry_bank first_geometry{resources::engine_data_asset()}, second_geometry{resources::engine_data_asset()};
  auto const cockpit{resources::decode_cockpit_mask(resources::cockpit_asset())};
  game::session first{first_geometry, world::decode_theatre(resources::morning_asset())};
  game::session second{second_geometry, world::decode_theatre(resources::morning_asset())};
  graphics::scene_renderer first_renderer{first_geometry, cockpit}, second_renderer{second_geometry, cockpit};
  corncob::render::framebuffer first_frame, second_frame, preview;
  first_renderer.initialise(first.camera().position, first.random_stream());
  second_renderer.initialise(second.camera().position, second.random_stream());
  unsigned int draws{0};
  auto draw_first{[&]{
    first_renderer.render(first.world(), first.camera(), first.frame_number(), static_cast<uint16_t>(first.timer()), first.random_stream(), first_frame);
    ++draws;
  }};
  auto draw_second{[&]{
    second_renderer.render(second.world(), second.camera(), second.frame_number(), static_cast<uint16_t>(second.timer()), second.random_stream(), second_frame);
  }};
  for(unsigned int step{0}; step != 60; ++step) {
    auto const before{first.pilot().position};
    game::input_state const input{
      .walk_forward{true},
    };
    auto check_then_draw{[&]{
      if(first.pilot().position != before) throw std::runtime_error{"scene drawing occurred after player movement"};
      draw_first();
    }};
    first.advance(input, std::function_ref<void()>{check_then_draw});
    for(unsigned int refresh{0}; refresh != step % 5; ++refresh) {
      second_renderer.preview(second.world(), second.camera(), second.frame_number(), static_cast<uint16_t>(second.timer()), second.random_stream(), preview);
    }
    second.advance(input, std::function_ref<void()>{draw_second});
    if(first.random_stream().seed() != second.random_stream().seed() || first.pilot().position != second.pilot().position
      || first.results().encode() != second.results().encode() || !std::ranges::equal(first_frame.pixels, second_frame.pixels, [](auto const &a, auto const &b){
        return a.red == b.red && a.green == b.green && a.blue == b.blue && a.alpha == b.alpha;
      })) {
      throw std::runtime_error{"extra window refreshes changed gameplay or a rendered simulation frame"};
    }
  }
  game::session pedestrian{first_geometry, world::theatre{}};
  pedestrian.advance({
    .walk_forward{true},
  });
  if(pedestrian.camera().gunsight) throw std::runtime_error{"walking did not hide the on-foot gunsight"};
  pedestrian.advance({});
  if(!pedestrian.camera().gunsight) throw std::runtime_error{"stopping did not restore the on-foot gunsight"};
  first.advance({}, std::function_ref<void()>{draw_first});
  auto const seed{first.random_stream().seed()};
  first.press(game::action::pause);
  first.advance({}, std::function_ref<void()>{draw_first});
  for(unsigned int refresh{0}; refresh != 4; ++refresh) {
    first_renderer.preview(first.world(), first.camera(), first.frame_number(), static_cast<uint16_t>(first.timer()), first.random_stream(), preview);
  }
  if(draws != 61 || first.random_stream().seed() != seed) throw std::runtime_error{"pause advanced the frame pipeline or shared randomness"};
  first.press(game::action::pause);
  first.press(game::action::toggle_map);
  first.advance({}, std::function_ref<void()>{draw_first});
  if(draws != 62 || first.frame_number() != 61) throw std::runtime_error{"map inspection did not draw independently of the stopped flight frame"};
  game::session long_flight{first_geometry, world::theatre{}};
  for(unsigned int step{0}; step != 16'384; ++step) long_flight.advance({});
  if(long_flight.frame_number() != 128) throw std::runtime_error{"connected sortie did not restart its animation clock"};
  std::cout << "512 native frame-clock transitions, simulation-stage drawing and host-refresh-independent gameplay match" << std::endl;
  return EXIT_SUCCESS;
}

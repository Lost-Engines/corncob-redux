#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include "game/session.h"
#include "graphics/scene.h"
#include "maths/random.h"
#include "resources/assets.h"

int main(int const argc, char const *const argv[]) {
  /// Exercise sustained mixed controls, weapons, ejection and rendering around every theatre's objectives, without replacing the actual simulation
  namespace resources = corncob::resources;
  namespace game = corncob::game;
  namespace world = corncob::world;
  namespace graphics = corncob::graphics;
  namespace maths = corncob::maths;
  resources::geometry_bank geometry{resources::engine_data_asset()};
  auto const cockpit{resources::decode_cockpit_mask(resources::cockpit_asset())};
  graphics::scene_renderer renderer{geometry, cockpit};
  if(argc > 2 || (argc == 2 && std::string_view{argv[1]} != "--draw-every-frame")) throw std::invalid_argument{"optional --draw-every-frame expected"};
  bool const full_frames{argc == 2};
  corncob::render::framebuffer output;
  maths::random_source input_random{0x454e4455};
  std::array const worlds{resources::morning_asset(), resources::dusk_asset(), resources::wasteland_asset(), resources::white_sands_asset()};
  unsigned int updates{0}, frames{0};
  for(auto const source : worlds) for(unsigned int start{0}; start != 8; ++start) {
    auto theatre{world::decode_theatre(source)};
    std::vector<maths::world_position> locations;
    for(auto const &object : theatre.objects) if(object.type == world::object_type::generator_orb || object.type == world::object_type::objective_saucer)
      locations.emplace_back(object.position);
    auto const plane{std::ranges::find_if(theatre.objects, [](auto const &object){return object.type == world::object_type::aircraft;})};
    if(plane == theatre.objects.end() || locations.empty()) throw std::runtime_error{"endurance theatre lacks an aircraft or objective"};
    theatre.start = maths::world_position{plane->position};
    theatre.start.y += 3000;
    game::session flight{geometry, std::move(theatre)};
    flight.set_experimental_clearance(true);
    flight.set_invincible(true);
    for(unsigned int ready{0}; ready != 8 && flight.player() == game::player_mode::on_foot; ++ready) flight.advance({});
    if(flight.player() != game::player_mode::in_aircraft) throw std::runtime_error{"endurance pilot could not board"};
    auto const &site{locations[start % locations.size()]};
    flight.relocate({
      .title{"Endurance start"},
      .position{maths::world_position{site.x, site.y - 150'000, 120'000}},
      .heading{input_random.next()},
    });
    flight.press(game::action::toggle_messages);
    graphics::scene_renderer full_renderer{geometry, cockpit};
    if(full_frames) full_renderer.initialise(flight.camera().position, flight.random_stream());
    auto draw_one{[&]{
      full_renderer.render(flight.world(), flight.camera(), flight.frame_number(), static_cast<uint16_t>(flight.timer()), flight.random_stream(), output);
      ++frames;
    }};
    for(unsigned int tick{0}; tick != 1000 && !flight.finished(); ++tick) {
      if(tick % 47 == 0) flight.press(game::action::drop_bomb);
      if(tick % 131 == 0) flight.press(game::action::toggle_flaps);
      if(tick == 500) flight.press(game::action::eject);
      if(tick == 530) flight.press(game::action::parachute);
      if(tick == 650) flight.press(game::action::toggle_remote);
      if(tick == 800) flight.press(game::action::call_rescue);
      game::input_state const input{
        .stick{
          .roll{maths::word(input_random.below(1025) - 512)},
          .pitch{maths::word(input_random.below(1025) - 512)},
          .rudder{maths::word(input_random.below(1025) - 512)},
        },
        .walk_forward{tick % 3 == 0},
        .jump{tick % 64 == 0},
        .weapons{
          .gun{true},
          .cannon{tick % 2 == 0},
          .missile{true},
        },
      };
      if(full_frames) flight.advance(input, std::function_ref<void()>{draw_one});
      else flight.advance(input);
      if(flight.take_tower()) flight.resume_from_tower();
      ++updates;
      if(!full_frames && tick % 127 == 0) {
        geometry.animate(flight.frame_number());
        renderer.render(flight.world(), flight.camera(), flight.frame_number(), static_cast<uint16_t>(flight.timer()), flight.random_stream(), output);
        ++frames;
      }
    }
    auto const retained{flight.snapshot()};
    auto const encoded{world::encode_theatre(retained)};
    (void)world::decode_theatre(encoded);
    (void)game::sortie_statistics::decode(flight.results().encode());
  }
  if(updates < 25'000 || frames < 200) throw std::runtime_error{"endurance runs ended before providing sustained coverage"};
  std::cout << updates << " simulation updates and " << frames << " rendered frames across 32 original-theatre starts" << std::endl;
  return EXIT_SUCCESS;
}

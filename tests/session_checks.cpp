#include <algorithm>
#include <bit>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include "game/session.h"
#include "resources/assets.h"

int main(int const argc, char const *const argv[]) {
  /// Drive the real White Sands data through walking, boarding, clearance, pause and powered flight
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset()};
  corncob::game::session game{geometry, corncob::world::decode_theatre(corncob::resources::white_sands_asset())};
  auto const initial{game.camera()};
  if(initial.cockpit || game.player() != corncob::game::player_mode::on_foot) throw std::runtime_error{"READUNIV must begin on foot"};
  game.press(corncob::game::action::plant_charge);
  for(unsigned int tick{0}; tick < 10; ++tick) game.advance({.weapons{.gun{true}}});
  if(game.sounds().gunshots != 0 || std::ranges::any_of(game.world().objects, [](auto const &object){
    return object.type == corncob::world::object_type::bomb && object.has(corncob::world::object_flag::present) && object.has(corncob::world::object_flag::special);
  })) throw std::runtime_error{"White Sands pilot received weapons before leaving an aircraft"};
  auto const plane{std::ranges::find_if(game.world().objects, [](auto const &object){
    return object.type == corncob::world::object_type::aircraft && (object.flags & 0x2000u) != 0;
  })};
  if(plane == game.world().objects.end()) throw std::runtime_error{"White Sands experimental aircraft missing"};
  auto const target{plane->position};
  for(unsigned int frame{0}; frame != 2000 && game.player() == corncob::game::player_mode::on_foot; ++frame) {
    auto const position{game.pilot().position};
    auto const desired{corncob::maths::arctangent(corncob::maths::word(target.x - position.x), corncob::maths::word(target.y - position.y))};
    int const error{std::bit_cast<int16_t>(static_cast<uint16_t>(desired - game.pilot().attitude.heading))};
    game.advance({
      .stick{
        .roll{static_cast<int16_t>(-std::clamp(error, -512, 512))},
      },
      .walk_forward{std::abs(error) < 2048},
    });
  }
  if(game.player() != corncob::game::player_mode::in_aircraft) throw std::runtime_error{"scripted pilot could not board the White Sands aircraft"};
  auto const runway_heading{game.craft().attitude.heading};
  game.press(corncob::game::action::toggle_look);
  for(unsigned int frame{0}; frame != 16; ++frame) game.advance({
    .stick{
      .roll{-512},
    },
    .view{
      .left{true},
    },
  });
  if(!game.looking_around() || game.craft().attitude.heading != runway_heading || std::abs(static_cast<int>(corncob::maths::word(game.camera().attitude.heading - runway_heading))) < 15'000) throw std::runtime_error{"keypad look altered the aircraft instead of the camera"};
  game.press(corncob::game::action::toggle_look);
  game.advance({});
  if(game.looking_around() || game.camera().attitude.heading != game.craft().attitude.heading) throw std::runtime_error{"leaving look mode did not restore the forward view"};
  game.press(corncob::game::action::throttle_up);
  if(game.craft().throttle != 20) throw std::runtime_error{"experimental clearance was bypassed"};
  auto const paused_timer{game.timer()};
  game.press(corncob::game::action::pause);
  game.advance({});
  if(game.timer() != paused_timer) throw std::runtime_error{"pause advanced physical time"};
  game.press(corncob::game::action::pause);
  game.set_experimental_clearance(true);
  auto const start{game.craft().position};
  for(unsigned int i{0}; i != 600 && game.player() == corncob::game::player_mode::in_aircraft; ++i) {
    game.advance({
      .stick{
        .pitch{static_cast<int16_t>(i > 250 ? std::clamp((std::bit_cast<int16_t>(game.craft().attitude.pitch) + 2500) / 8, -256, 256) : 0)},
      },
      .throttle_up{i < 10},
    });
    if(i % 100 == 0) std::cout << "Flight frame " << i << ": z=" << game.craft().position.z << " v=" << game.craft().body_velocity.x
      << " pitch=" << std::bit_cast<int16_t>(game.craft().attitude.pitch) << " throttle=" << game.craft().throttle << std::endl;
  }
  if(game.player() != corncob::game::player_mode::in_aircraft || game.craft().position.z < 5000) throw std::runtime_error{"scripted takeoff did not achieve sustained flight"};
  std::cout << "Final mode " << std::to_underlying(game.player()) << ": " << game.message() << std::endl;
  if(game.craft().position == start) throw std::runtime_error{"throttle did not move the aircraft"};
  if(argc == 2) {
    auto const cockpit{corncob::resources::decode_cockpit_mask(corncob::resources::cockpit_asset())};
    corncob::graphics::scene_renderer renderer{geometry, cockpit};
    corncob::render::framebuffer image;
    corncob::graphics::instrument_telemetry const telemetry{
      .aircraft{game.craft()},
      .controls{},
    };
    renderer.render(game.world(), game.camera(), game.frame_number(), static_cast<uint16_t>(game.timer()), game.random_stream(), image, &telemetry);
    std::ofstream capture{argv[1], std::ios::binary};
    capture.exceptions(std::ios::failbit | std::ios::badbit);
    capture << "P6\n640 350\n255\n";
    for(auto const &pixel : image.pixels) {
      capture.put(static_cast<char>(pixel.red));
      capture.put(static_cast<char>(pixel.green));
      capture.put(static_cast<char>(pixel.blue));
    }
  }
  auto const before_stick_view{game.craft().attitude.roll};
  for(unsigned int frame{0}; frame != 12; ++frame) game.advance({
    .stick{
      .roll{256},
    },
    .joystick_controlled{true},
    .view{
      .left{true},
      .modifier{true},
    },
  });
  if(game.craft().attitude.roll == before_stick_view || !game.looking_around()) throw std::runtime_error{"looking sideways suppressed physical joystick flight controls"};
  game.advance({});
  auto const airborne{game.craft().position};
  game.press(corncob::game::action::eject);
  if(game.player() != corncob::game::player_mode::on_foot || game.camera().cockpit) throw std::runtime_error{"airborne ejection did not change viewpoint"};
  game.press(corncob::game::action::parachute);
  if(!game.pilot().parachute) throw std::runtime_error{"ripcord failed"};
  game.press(corncob::game::action::toggle_remote);
  for(unsigned int i{0}; i != 500 && game.player() == corncob::game::player_mode::on_foot; ++i) game.advance({});
  if(game.craft().position == airborne) throw std::runtime_error{"abandoned aircraft froze"};
  if(game.player() != corncob::game::player_mode::on_foot) throw std::runtime_error{"open parachute failed to protect pilot"};
  std::cout << "Scripted White Sands boarding, clearance, pause and powered-flight path exercised" << std::endl;
  return EXIT_SUCCESS;
}

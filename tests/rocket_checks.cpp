#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include "game/rocket.h"
#include "game/session.h"
#include "resources/assets.h"
#include "tests/reference_reader.h"
#include "world/training.h"

int main(int const argc, char const *const argv[]) {
  /// Verify real BIOS-clock recharge, including native overshoot and signed-storage edge cases
  if(argc != 2) throw std::invalid_argument{"rocket fixture required"};
  corncob::tests::reference_reader input{argv[1], "CORNROC1"};
  unsigned int const cases{input.dword()};
  for(unsigned int i{0}; i != cases; ++i) {
    auto const old_tick{input.word()}, tick{input.word()};
    auto const stock{input.signed_word()};
    auto const elapsed{input.word()}, actual_elapsed{input.word()};
    bool const burning{input.byte() != 0};
    auto const expected_stock{input.signed_word()};
    auto const expected_tick{input.word()};
    auto const expected_colour{input.byte()};
    corncob::flight::aircraft_state aircraft;
    aircraft.rocket_ticks = stock;
    aircraft.rocket = burning;
    corncob::game::rocket_charge charge{
      .previous_tick{old_tick},
    };
    bool const ready{charge.advance(aircraft, tick, elapsed, actual_elapsed)};
    if(aircraft.rocket_ticks != expected_stock || charge.previous_tick != expected_tick || charge.colour != expected_colour || ready != (expected_colour == 10))
      throw std::runtime_error{"native rocket update " + std::to_string(i)};
  }
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset()};
  corncob::world::training_setup setup;
  setup.kla.count = setup.aaa.count = setup.vehicles.count = 0;
  corncob::game::session flight{geometry, corncob::world::generate_training(geometry, setup)};
  flight.set_invincible(true);
  flight.relocate({
    .title{"Rocket recharge inspection"},
    .position{0, 0, 500'000},
    .heading{0},
  });
  flight.press(corncob::game::action::rocket);
  for(unsigned int frame{0}; frame != 4 && !flight.craft().rocket; ++frame) flight.advance({});
  if(!flight.craft().rocket) throw std::runtime_error{"charged rocket did not ignite"};
  for(unsigned int frame{0}; frame != 1200 && flight.craft().rocket; ++frame) flight.advance({});
  if(flight.craft().rocket || flight.craft().rocket_ticks >= corncob::game::rocket_charge::capacity) throw std::runtime_error{"rocket charge was not consumed: stock " + std::to_string(flight.craft().rocket_ticks) + " mode " + std::to_string(std::to_underlying(flight.player())) + " height " + std::to_string(flight.craft().position.z)};
  auto const discharged{flight.craft().rocket_ticks};
  for(unsigned int frame{0}; frame != 128; ++frame) flight.advance({});
  if(flight.craft().rocket_ticks <= discharged) throw std::runtime_error{"connected rocket never recharged"};
  flight.press(corncob::game::action::rocket);
  flight.advance({});
  if(flight.craft().rocket) throw std::runtime_error{"partially charged rocket ignited before becoming ready"};
  bool refired{false};
  for(unsigned int frame{0}; frame != 4000 && !refired; ++frame) {
    flight.advance({});
    refired = flight.craft().rocket;
  }
  if(!refired) throw std::runtime_error{"rocket did not become ready and accept its pending ignition request"};
  std::cout << cases << " original rocket recharge and ready-indicator updates match" << std::endl;
  return EXIT_SUCCESS;
}

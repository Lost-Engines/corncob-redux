#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include "game/session.h"
#include "game/sortie_clock.h"
#include "resources/assets.h"
#include "tests/reference_reader.h"

int main(int const argc, char const *const argv[]) {
  /// Compare the complete native clock conversion and continuation across a suspended interval
  if(argc != 2) throw std::invalid_argument{"sortie-clock fixture required"};
  corncob::tests::reference_reader input{argv[1], "CORNTMR1"};
  unsigned int const cases{input.dword()};
  for(unsigned int i{0}; i != cases; ++i) {
    corncob::game::sortie_clock clock{input.word()};
    auto const first{input.dword()};
    auto const second{input.dword()};
    auto const first_seconds{input.word()};
    auto const second_seconds{input.word()};
    clock.advance(first);
    if(clock.elapsed() != first_seconds) throw std::runtime_error{"native sortie-clock first segment " + std::to_string(i)};
    clock.suspend();
    clock.advance(second);
    if(clock.elapsed() != second_seconds) throw std::runtime_error{"native sortie-clock continuation " + std::to_string(i)};
  }
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset()};
  corncob::game::session flight{geometry, corncob::world::theatre{}};
  for(unsigned int frame{0}; frame != 90; ++frame) flight.advance({});
  if(flight.results().elapsed_seconds != 3) throw std::runtime_error{"elapsed clock was not connected to flight results"};
  flight.press(corncob::game::action::pause);
  for(unsigned int frame{0}; frame != 90; ++frame) flight.advance({});
  if(flight.results().elapsed_seconds != 3) throw std::runtime_error{"paused flight increased recorded sortie time"};
  flight.press(corncob::game::action::pause);
  flight.press(corncob::game::action::toggle_map);
  for(unsigned int frame{0}; frame != 90; ++frame) flight.advance({});
  if(flight.results().elapsed_seconds != 6) throw std::runtime_error{"map inspection lost its original elapsed-clock accounting"};
  std::cout << cases << " original two-segment sortie clocks match" << std::endl;
  return EXIT_SUCCESS;
}

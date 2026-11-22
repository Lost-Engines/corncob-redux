#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "audio/effects.h"
#include "game/session.h"
#include "resources/assets.h"
#include "tests/reference_reader.h"

namespace {

bool proximity_register(uint8_t const address) noexcept {
  /// Restrict comparisons to the original proximity block's operator and channel registers
  switch(address) {
  case 0x28:
  case 0x2b:
  case 0x48:
  case 0x4b:
  case 0x4c:
  case 0x68:
  case 0x6b:
  case 0x88:
  case 0x8b:
  case 0xa3:
  case 0xa4:
  case 0xb3:
  case 0xb4:
  case 0xc3:
  case 0xe8:
  case 0xeb:
    return true;
  default:
    return false;
  }
}

} // anonymous namespace

int main(int const argc, char const *const argv[]) {
  /// Compare connected register streams from the original shared proximity channels
  if(argc != 2) throw std::invalid_argument{"soundscape fixture required"};
  corncob::resources::collection resources{corncob::resources::front_end_asset()};
  corncob::audio::effects sound{resources};
  corncob::tests::reference_reader input{argv[1], "CORNSFX1"};
  std::array<float, 2> drain;
  sound.update({
    .tick{1},
    .active{true},
  });
  sound.render(drain);
  auto const cases{input.dword()};
  unsigned int registers{0};
  for(unsigned int i{0}; i != cases; ++i) {
    sound.update({
      .tick{(i + 1) * 40},
      .orb_distance{input.word()},
      .orb_frequency{input.byte()},
      .kla_distance{input.word()},
      .kla_quality{input.byte()},
      .saucer_distance{input.word()},
      .saucer_frequency{input.byte()},
      .active{true},
    });
    std::vector<corncob::audio::register_write> actual;
    for(auto const &write : sound.pending_registers()) {
      if(proximity_register(write.address)) actual.push_back(write);
    }
    unsigned int const count{input.word()};
    if(actual.size() != count) {
      throw std::runtime_error{"native proximity register count in frame " + std::to_string(i)
        + ": got " + std::to_string(actual.size()) + ", expected " + std::to_string(count)};
    }
    for(unsigned int reg{0}; reg != count; ++reg) {
      auto const address{input.byte()};
      auto const value{input.byte()};
      if(actual[reg].address != address || actual[reg].value != value) {
        throw std::runtime_error{"native proximity register in frame " + std::to_string(i) + ", write " + std::to_string(reg)};
      }
    }
    registers += count;
    sound.render(drain);
  }
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset()};
  corncob::world::theatre theatre;
  theatre.start = corncob::maths::world_position{0, 3000, 720};
  theatre.objects.push_back({
    .position{0, 0, 10'000},
    .attitude{},
    .type{corncob::world::object_type::generator_orb},
    .flags{geometry.definition(corncob::world::object_type::generator_orb).flags},
    .timer{32'000},
  });
  corncob::game::session game{geometry, std::move(theatre)};
  game.advance({});
  if(corncob::audio::observe(game).orb_distance != 63) throw std::runtime_error{"proximity sound consumed the current actor pass"};
  game.advance({});
  if(corncob::audio::observe(game).orb_distance >= 63) throw std::runtime_error{"proximity sound did not receive the preceding actor pass"};
  std::cout << cases << " connected original proximity frames and " << registers << " OPL register writes match" << std::endl;
  return EXIT_SUCCESS;
}

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include "game/combat.h"
#include "resources/assets.h"
#include "resources/binary_reader.h"
#include "tests/actor_record.h"

int main(int const argc, char const *const argv[]) {
  /// Compare constructor output rather than an approximate visual or a chosen particle count
  if(argc != 2) throw std::invalid_argument{"debris_checks requires its native fixture"};
  std::ifstream input{argv[1], std::ios::binary};
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>{input}, {}};
  corncob::resources::binary_reader const data{bytes};
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset()};
  unsigned int offset{12};
  unsigned int particles{0};
  for(unsigned int trial{0}; trial != data.dword(8); ++trial) {
    corncob::world::theatre theatre;
    corncob::game::combat weapons{geometry, theatre, data.dword(offset + 8)};
    auto const pattern{static_cast<corncob::game::fragment_pattern>(data.word(offset))};
    unsigned int const requested{data.word(offset + 2)};
    corncob::flight::time_step const step{
      .elapsed{data.signed_word(offset + 4)},
      .normal{data.signed_word(offset + 6)},
    };
    offset += 12;
    corncob::world::placed_object const source{
      .position{data.signed_dword(offset), data.signed_dword(offset + 4), data.signed_dword(offset + 8)},
      .attitude{},
      .velocity{data.signed_word(offset + 46), data.signed_word(offset + 48), data.signed_word(offset + 50)},
    };
    offset += 74;
    unsigned int const count{data.dword(offset)};
    offset += 4;
    if(std::to_underlying(pattern) == 6) {
      weapons.explosion(source.position, static_cast<uint16_t>(requested));
    } else if(std::to_underlying(pattern) >= 4) {
      weapons.shatter(source, requested, std::to_underlying(pattern) == 4);
    } else {
      weapons.scatter(source, requested, pattern, step);
    }
    unsigned int actual{0};
    for(auto const &object : theatre.objects) {
      if(!object.has(corncob::world::object_flag::present)) continue;
      if(actual++ >= count) throw std::runtime_error{"too many debris particles"};
      corncob::tests::compare_actor(object, corncob::tests::read_actor(data, offset), trial, "debris");
      offset += 74;
      ++particles;
    }
    if(actual != count) throw std::runtime_error{"debris count differs"};
    if(weapons.random_stream().seed() != data.dword(offset)) throw std::runtime_error{"debris random continuation differs"};
    offset += 4;
  }
  if(offset != bytes.size()) throw std::runtime_error{"unconsumed debris reference bytes"};
  std::cout << particles << " particles match 192 original debris/shard/explosion constructor runs" << std::endl;
  return EXIT_SUCCESS;
}

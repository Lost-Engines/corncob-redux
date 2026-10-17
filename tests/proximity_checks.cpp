#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include "game/actors.h"
#include "resources/assets.h"
#include "tests/actor_record.h"

int main(int const argc, char const *const argv[]) {
  /// Compare native handler-derived sound state, retaining the original ordering of multiple actors
  if(argc != 2) throw std::invalid_argument{"actor-proximity fixture required"};
  std::ifstream file{argv[1], std::ios::binary};
  std::vector<uint8_t> const bytes{std::istreambuf_iterator<char>{file}, {}};
  corncob::resources::binary_reader const data{bytes};
  if(bytes.size() < 12 || std::string{bytes.begin(), bytes.begin() + 8} != "CORNPRX1") throw std::invalid_argument{"invalid actor-proximity fixture"};
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset()};
  unsigned int const cases{data.dword(8)};
  unsigned int offset{12};
  for(unsigned int i{0}; i != cases; ++i) {
    corncob::maths::world_position const observer{data.signed_dword(offset), data.signed_dword(offset + 4), data.signed_dword(offset + 8)};
    corncob::maths::world_position const target{data.signed_dword(offset + 12), data.signed_dword(offset + 16), data.signed_dword(offset + 20)};
    bool const on_foot{data.byte(offset + 24) != 0};
    unsigned int const count{data.byte(offset + 25)};
    offset += 26;
    corncob::world::theatre theatre;
    theatre.start = corncob::maths::world_position{target};
    corncob::game::combat weapons{geometry, theatre};
    corncob::game::actors actors{geometry, theatre, weapons};
    for(unsigned int actor{0}; actor != count; ++actor, offset += 74) theatre.objects[actor] = corncob::tests::read_actor(data, offset);
    corncob::maths::fixed_vector const velocity{0, 0, 0};
    auto const &actual{actors.advance({observer, target, velocity, on_foot, 0}, {}).sound};
    if(count == 1 && (actual.orb_distance != data.word(offset) || actual.kla_distance != data.word(offset + 2)
      || actual.kla_quality != data.byte(offset + 4) || actual.saucer_distance != data.word(offset + 5)
      || actual.saucer_frequency != data.byte(offset + 7)))
      throw std::runtime_error{"native actor-proximity case " + std::to_string(i) + ": orb=" + std::to_string(actual.orb_distance)
        + ", KLA=" + std::to_string(actual.kla_distance) + ", saucer=" + std::to_string(actual.saucer_distance)
        + ", frequency=" + std::to_string(actual.saucer_frequency)};
    offset += 8;
  }
  if(offset != bytes.size()) throw std::runtime_error{"actor-proximity fixture length mismatch"};
  std::cout << cases << " proximity cases traversed; single-actor native results retained, aggregation covered by Redux tests" << std::endl;
  return EXIT_SUCCESS;
}

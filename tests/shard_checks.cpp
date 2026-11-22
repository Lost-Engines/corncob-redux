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
  /// Verify the native pool-slot flame rule, low-word ground test, colour binding and random continuation
  if(argc != 2) throw std::invalid_argument{"shard fixture required"};
  std::ifstream file{argv[1], std::ios::binary};
  std::vector<uint8_t> const bytes{std::istreambuf_iterator<char>{file}, {}};
  corncob::resources::binary_reader const data{bytes};
  if(bytes.size() < 12 || std::string{bytes.begin(), bytes.begin() + 8} != "CORNSHR1") throw std::invalid_argument{"invalid shard fixture"};
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset()};
  unsigned int const cases{data.dword(8)};
  unsigned int offset{12};
  for(unsigned int i{0}; i != cases; ++i, offset += 86) {
    corncob::world::theatre theatre;
    corncob::game::combat weapons{geometry, theatre, data.dword(offset + 2)};
    corncob::game::actors actors{geometry, theatre, weapons};
    auto &object{theatre.objects[data.word(offset)]};
    object = corncob::tests::read_actor(data, offset + 6);
    corncob::maths::world_position const observer{0, 0, 0};
    corncob::maths::fixed_vector const velocity{0, 0, 0};
    actors.advance({observer, observer, velocity, false, 0}, {});
    if(!object.shard_colour || *object.shard_colour != data.word(offset + 80) || weapons.random_stream().seed() != data.dword(offset + 82)) {
      throw std::runtime_error{"native shard colour/random case " + std::to_string(i)};
    }
    geometry.prepare_colours(object, 0, 0, 0, false);
    if(geometry.colour(0x71cf) != *object.shard_colour) throw std::runtime_error{"shard colour was not applied to its primitives"};
  }
  if(offset != bytes.size()) throw std::runtime_error{"shard fixture length mismatch"};
  std::cout << cases << " native shard flame/ground colours and random continuation match" << std::endl;
  return EXIT_SUCCESS;
}

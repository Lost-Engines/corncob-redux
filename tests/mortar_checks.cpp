#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include "game/actors.h"
#include "resources/assets.h"
#include "tests/actor_record.h"

int main(int const argc, char const *const argv[]) {
  /// Compare the complete original timer handler and emitted particles, including failed pool allocation
  if(argc != 2) throw std::invalid_argument{"mortar fixture required"};
  std::ifstream input{argv[1], std::ios::binary};
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>{input}, {}};
  corncob::resources::binary_reader const data{bytes};
  if(bytes.size() < 12 || std::string{bytes.begin(), bytes.begin() + 8} != "CORNMRT1") throw std::runtime_error{"invalid mortar fixture"};
  namespace world = corncob::world;
  namespace game = corncob::game;
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset()};
  unsigned int offset{12};
  unsigned int particles{0};
  for(unsigned int i{0}; i != data.dword(8); ++i) {
    auto const initial{corncob::tests::read_actor(data, offset)};
    corncob::maths::world_position const observer{data.signed_dword(offset + 74), data.signed_dword(offset + 78), data.signed_dword(offset + 82)};
    bool const full{data.byte(offset + 92) != 0};
    world::theatre theatre;
    game::combat weapons{geometry, theatre, data.dword(offset + 88)};
    game::actors actors{geometry, theatre, weapons};
    for(unsigned int n{0}; n != 320; ++n) weapons.spawn(world::object_type::aircraft, {0, 0, 0});
    for(auto &object : theatre.objects) object.flags = full ? 1 : 0;
    theatre.objects[0] = initial;
    corncob::maths::fixed_vector const velocity{0, 0, 0};
    actors.advance({observer, observer, velocity, false, 0}, {data.signed_word(offset + 86), 5});
    corncob::tests::compare_actor(theatre.objects[0], corncob::tests::read_actor(data, offset + 93), i, "mortar");
    if(weapons.random_stream().seed() != data.dword(offset + 167)) throw std::runtime_error{"native mortar random continuation " + std::to_string(i)};
    unsigned int const count{data.byte(offset + 171)};
    offset += 172;
    unsigned int actual_count{0};
    if(!full) {
      for(unsigned int slot{1}; slot != 320; ++slot) {
        if(theatre.objects[slot].flags != 0) ++actual_count;
      }
    }
    if(actual_count != count) throw std::runtime_error{"native mortar particle count " + std::to_string(i)};
    for(unsigned int n{0}; n != count; ++n) {
      corncob::tests::compare_actor(theatre.objects[n + 1], corncob::tests::read_actor(data, offset), i, "particle");
      offset += 74;
      ++particles;
    }
  }
  if(offset != bytes.size()) throw std::runtime_error{"unconsumed mortar fixture bytes"};
  std::cout << "256 original mortar updates and " << particles << " complete particles match" << std::endl;
  return EXIT_SUCCESS;
}

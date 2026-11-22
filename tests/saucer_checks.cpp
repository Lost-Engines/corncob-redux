#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include "game/actors.h"
#include "resources/assets.h"
#include "resources/binary_reader.h"

int main(int const argc, char const *const argv[]) {
  /// Compare complete living saucer movement through the real actor dispatcher, with original radar/weapon construction isolated in the reference
  if(argc != 2) throw std::invalid_argument{"saucer fixture required"};
  std::ifstream file{argv[1], std::ios::binary};
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>{file}, {}};
  corncob::resources::binary_reader const data{bytes};
  if(bytes.size() != 82'956 || std::string{bytes.begin(), bytes.begin() + 8} != "CORNSAU1") throw std::runtime_error{"invalid saucer fixture"};
  namespace resources = corncob::resources;
  namespace game = corncob::game;
  namespace maths = corncob::maths;
  namespace world = corncob::world;
  resources::geometry_bank geometry{resources::engine_data_asset()};
  unsigned int offset{12};
  for(unsigned int i{0}; i != 512; ++i) {
    world::theatre theatre;
    theatre.start = maths::world_position{data.signed_dword(offset + 74), data.signed_dword(offset + 78), data.signed_dword(offset + 82)};
    game::combat weapons{geometry, theatre};
    game::actors actors{geometry, theatre, weapons};
    auto const index{*weapons.spawn(static_cast<world::object_type>(data.word(offset + 72)), {0, 0, 0})};
    auto &object{theatre.objects[index]};
    object.position = maths::world_position{data.signed_dword(offset), data.signed_dword(offset + 4), data.signed_dword(offset + 8)};
    object.attitude = {
      .heading{data.word(offset + 12)},
      .pitch{data.word(offset + 14)},
      .roll{data.word(offset + 16)},
    };
    object.flags = data.word(offset + 24);
    object.health = data.byte(offset + 27);
    object.velocity = data.vector(offset + 46);
    object.timer = data.word(offset + 52);
    maths::world_position const observer{object.position};
    maths::fixed_vector const target_velocity{0, 0, 0};
    actors.advance({observer, observer, target_velocity, false, 0}, {data.signed_word(offset + 86), 5});
    unsigned int const expected{offset + 88};
    std::array<uint16_t, 8> const actual{object.attitude.heading, object.attitude.pitch, object.attitude.roll,
      object.flags, static_cast<uint16_t>(object.velocity.x), static_cast<uint16_t>(object.velocity.y), static_cast<uint16_t>(object.velocity.z), object.timer};
    std::array<unsigned int, 8> constexpr fields{12, 14, 16, 24, 46, 48, 50, 52};
    for(unsigned int field{0}; field != fields.size(); ++field) if(actual[field] != data.word(expected + fields[field])) {
      throw std::runtime_error{"native saucer case " + std::to_string(i) + " field " + std::to_string(fields[field]) + ": got " + std::to_string(actual[field]) + ", expected " + std::to_string(data.word(expected + fields[field]))};
    }
    if(object.health != data.byte(expected + 27)) throw std::runtime_error{"native objective saucer health mismatch"};
    offset += 162;
  }
  std::cout << "512 original living saucer movement passes match" << std::endl;
  return EXIT_SUCCESS;
}

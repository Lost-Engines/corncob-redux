#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include "game/actors.h"
#include "resources/assets.h"
#include "resources/binary_reader.h"

int main(int const argc, char const *const argv[]) {
  /// Compare native AAA, balloon, bee and alien updates, including any initialized shell rather than only the visible actor
  if(argc != 2) throw std::invalid_argument{"threat fixture required"};
  std::ifstream file{argv[1], std::ios::binary};
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>{file}, {}};
  corncob::resources::binary_reader const data{bytes};
  if(bytes.size() != 380'940 || std::string{bytes.begin(), bytes.begin() + 8} != "CORNTHR1") throw std::runtime_error{"invalid threat fixture"};
  namespace resources = corncob::resources;
  namespace game = corncob::game;
  namespace maths = corncob::maths;
  namespace world = corncob::world;
  resources::geometry_bank geometry{resources::engine_data_asset()};
  unsigned int offset{12}, shells{0};
  for(unsigned int i{0}; i != 1536; ++i, offset += 248) {
    world::theatre theatre;
    game::combat weapons{geometry, theatre, data.dword(offset + 95)};
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
    object.velocity = data.vector(offset + 46);
    object.timer = data.word(offset + 52);
    maths::world_position const observer{data.signed_dword(offset + 74), data.signed_dword(offset + 78), data.signed_dword(offset + 82)};
    maths::fixed_vector const velocity{data.vector(offset + 86)};
    maths::world_position const active_observer{object.type == world::object_type::alien ? observer : object.position};
    actors.advance({active_observer, observer, velocity, data.byte(offset + 94) != 0, 0}, {data.signed_word(offset + 92), 5});
    unsigned int const expected{offset + 99};
    std::array<uint16_t, 9> const actual{object.attitude.heading, object.attitude.pitch, object.attitude.roll,
      object.flags, static_cast<uint16_t>(object.velocity.x), static_cast<uint16_t>(object.velocity.y), static_cast<uint16_t>(object.velocity.z), object.timer,
      static_cast<uint16_t>(object.motion.gravity)};
    std::array<unsigned int, 9> constexpr fields{12, 14, 16, 24, 46, 48, 50, 52, 64};
    for(unsigned int field{0}; field != fields.size(); ++field) if(actual[field] != data.word(expected + fields[field])) {
      throw std::runtime_error{"native threat case " + std::to_string(i) + " field " + std::to_string(fields[field]) + ": got " + std::to_string(actual[field]) + ", expected " + std::to_string(data.word(expected + fields[field]))};
    }
    if(object.position.z != data.signed_dword(expected + 8)) throw std::runtime_error{"native threat height case " + std::to_string(i)};
    auto const shell{std::ranges::find_if(theatre.objects, [](auto const &candidate){return candidate.type == world::object_type::aa_shell && candidate.has(world::object_flag::present);})};
    bool const launched{shell != theatre.objects.end()};
    if(launched != (data.byte(expected + 74) != 0)) throw std::runtime_error{"native shell launch case " + std::to_string(i)};
    if(launched) {
      ++shells;
      unsigned int const shell_data{expected + 75};
      if(shell->position != maths::world_position{data.signed_dword(shell_data), data.signed_dword(shell_data + 4), data.signed_dword(shell_data + 8)}
        || shell->velocity != data.vector(shell_data + 46) || shell->timer != data.word(shell_data + 52)
        || shell->motion.dispersion != data.signed_word(shell_data + 60)) throw std::runtime_error{"native shell initialization case " + std::to_string(i)};
    }
  }
  std::cout << "1536 native threat handlers and " << shells << " shell initializations match" << std::endl;
  return EXIT_SUCCESS;
}

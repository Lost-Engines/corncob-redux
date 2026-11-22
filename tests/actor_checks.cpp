#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include "game/actors.h"
#include "resources/assets.h"
#include "tests/reference_reader.h"

namespace {

std::array<uint8_t, 74> record(corncob::tests::reference_reader &input) {
  /// Native actor layout stays in the fixture decoder, separate from the C++ ownership model
  std::array<uint8_t, 74> result;
  for(auto &byte : result) byte = input.byte();
  return result;
}
uint16_t word(std::array<uint8_t, 74> const &data, unsigned int const offset) {
  /// Read original little-endian words without alignment or aliasing assumptions
  return static_cast<uint16_t>(data[offset] | (data[offset + 1] << 8));
}

} // anonymous namespace

int main(int const argc, char const *const argv[]) {
  /// Exercise the live KLA actor handler with original native reference steering passes
  if(argc != 2) throw std::invalid_argument{"actor_checks requires a fixture path"};
  corncob::tests::reference_reader input{argv[1], "CORNACT1"};
  auto const cases{input.dword()};
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset()};
  for(unsigned int i{0}; i != cases; ++i) {
    auto const initial{record(input)};
    corncob::maths::world_position const target{input.signed_dword(), input.signed_dword(), input.signed_dword()};
    corncob::flight::time_step const step{
      .elapsed{input.signed_word()},
      .normal{5},
    };
    auto const expected{record(input)};
    corncob::world::theatre theatre;
    corncob::game::combat weapons{geometry, theatre};
    corncob::game::actors actors{geometry, theatre, weapons};
    auto const index{*weapons.spawn(corncob::world::object_type::kla, {0, 0, 0})};
    auto &object{theatre.objects[index]};
    for(unsigned int axis{0}; axis != 3; ++axis) {
      object.position[axis] = std::bit_cast<int32_t>(static_cast<uint32_t>(word(initial, axis * 4)) | (static_cast<uint32_t>(word(initial, axis * 4 + 2)) << 16));
      object.velocity[axis] = corncob::maths::word(word(initial, 46 + axis * 2));
    }
    object.attitude = {
      .heading{word(initial, 12)},
      .pitch{word(initial, 14)},
      .roll{word(initial, 16)},
    };
    object.flags = word(initial, 24);
    object.timer = word(initial, 52);
    object.motion.dispersion = corncob::maths::word(word(initial, 60));
    object.motion.drag_divisor = corncob::maths::word(word(initial, 62));
    object.motion.gravity = corncob::maths::word(word(initial, 64));
    corncob::maths::fixed_vector const velocity{0, 0, 0};
    actors.advance({target, target, velocity, false, 0}, step);
    std::array<uint16_t, 9> const actual{object.attitude.heading, object.attitude.pitch, object.attitude.roll,
      static_cast<uint16_t>(object.velocity.x), static_cast<uint16_t>(object.velocity.y), static_cast<uint16_t>(object.velocity.z),
      static_cast<uint16_t>(object.motion.dispersion), static_cast<uint16_t>(object.motion.drag_divisor), static_cast<uint16_t>(object.motion.gravity)};
    std::array<unsigned int, 9> constexpr fields{12, 14, 16, 46, 48, 50, 60, 62, 64};
    for(unsigned int field{0}; field != fields.size(); ++field) {
      auto const wanted{word(expected, fields[field])};
      if(actual[field] != wanted) {
        throw std::runtime_error{"native KLA case " + std::to_string(i) + " field " + std::to_string(fields[field])
          + ": expected " + std::to_string(wanted) + ", got " + std::to_string(actual[field])};
      }
    }
  }
  std::cout << cases << " native KLA steering passes match" << std::endl;
  return EXIT_SUCCESS;
}

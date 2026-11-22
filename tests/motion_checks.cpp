#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include "tests/reference_reader.h"
#include "world/motion.h"

namespace {

struct motion_case {
  corncob::world::placed_object object;
  bool rotates;
};
motion_case read_object(corncob::tests::reference_reader &input) {
  /// Decode the reference's explicit 74-byte native layout, never C++ object representation
  std::array<uint8_t, 74> data;
  for(auto &byte : data) byte = input.byte();
  auto word{[&](unsigned int const offset){return static_cast<uint16_t>(data[offset] | (data[offset + 1] << 8));}};
  motion_case result{};
  auto &object{result.object};
  for(unsigned int axis{0}; axis != 3; ++axis) {
    object.position[axis] = std::bit_cast<int32_t>(static_cast<uint32_t>(word(axis * 4)) | (static_cast<uint32_t>(word(axis * 4 + 2)) << 16));
    object.velocity[axis] = corncob::maths::word(word(46 + axis * 2));
    object.motion.spin[axis] = corncob::maths::word(word(54 + axis * 2));
  }
  object.attitude = {
    .heading{word(12)},
    .pitch{word(14)},
    .roll{word(16)},
  };
  result.rotates = data[26] != 0;
  object.motion.dispersion = corncob::maths::word(word(60));
  object.motion.drag_divisor = corncob::maths::word(word(62));
  object.motion.gravity = corncob::maths::word(word(64));
  return result;
}
void check(bool const condition, char const *const field, unsigned int const index) {
  /// Report the first native divergence in motion or collision geometry
  if(!condition) throw std::runtime_error{std::string{field} + " differs in case " + std::to_string(index)};
}

} // anonymous namespace

int main(int const argc, char const *const argv[]) {
  /// Exercise original PRNG-driven motion and all three strict box boundaries against machine code
  if(argc != 2) throw std::invalid_argument{"motion_checks requires a fixture path"};
  corncob::tests::reference_reader input{argv[1], "CORNMO01"};
  auto const motions{input.dword()};
  auto const boxes{input.dword()};
  for(unsigned int i{0}; i != motions; ++i) {
    auto actual{read_object(input)};
    corncob::flight::time_step const step{
      .elapsed{input.signed_word()},
      .normal{input.signed_word()},
    };
    auto const expected{read_object(input)};
    corncob::maths::random_source random;
    bool const grounded{corncob::world::advance_motion(actual.object, actual.rotates, random, step)};
    check(grounded == (input.byte() != 0), "ground contact", i);
    for(unsigned int axis{0}; axis != 3; ++axis) {
      check(actual.object.position[axis] == expected.object.position[axis], "position", i);
      check(actual.object.velocity[axis] == expected.object.velocity[axis], "velocity", i);
      check(actual.object.motion.spin[axis] == expected.object.motion.spin[axis], "spin", i);
    }
    check(actual.object.attitude.heading == expected.object.attitude.heading && actual.object.attitude.pitch == expected.object.attitude.pitch
      && actual.object.attitude.roll == expected.object.attitude.roll, "attitude", i);
    check(actual.object.motion.dispersion == expected.object.motion.dispersion && actual.object.motion.drag_divisor == expected.object.motion.drag_divisor
      && actual.object.motion.gravity == expected.object.motion.gravity, "motion profile", i);
  }
  for(unsigned int i{0}; i != boxes; ++i) {
    corncob::maths::world_position const a{input.signed_dword(), input.signed_dword(), input.signed_dword()};
    corncob::maths::fixed_vector const ae{input.signed_word(), input.signed_word(), input.signed_word()};
    corncob::maths::world_position const b{input.signed_dword(), input.signed_dword(), input.signed_dword()};
    corncob::maths::fixed_vector const be{input.signed_word(), input.signed_word(), input.signed_word()};
    check(corncob::world::boxes_overlap(a, ae, b, be) == (input.byte() != 0), "strict box overlap", i);
  }
  std::cout << motions << " native object motions and " << boxes << " strict box overlaps match" << std::endl;
  return EXIT_SUCCESS;
}

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include "game/combat.h"
#include "tests/reference_reader.h"

int main(int const argc, char const *const argv[]) {
  /// Compare the actual contact callbacks, including native fast-shot direction and reach tests
  if(argc != 2) throw std::invalid_argument{"collision_checks requires a fixture path"};
  corncob::tests::reference_reader input{argv[1], "CORNCOL1"};
  auto const count{input.dword()};
  for(unsigned int i{0}; i != count; ++i) {
    bool const fast{input.byte() != 0};
    corncob::world::placed_object a;
    corncob::world::placed_object b;
    for(unsigned int axis{0}; axis != 3; ++axis) a.position[axis] = input.signed_dword();
    for(unsigned int axis{0}; axis != 3; ++axis) b.position[axis] = input.signed_dword();
    for(unsigned int axis{0}; axis != 3; ++axis) a.velocity[axis] = input.signed_word();
    auto const ar{input.signed_word()};
    auto const br{input.signed_word()};
    a.collision_extents = corncob::maths::fixed_vector{ar, ar, ar};
    b.collision_extents = corncob::maths::fixed_vector{br, br, br};
    a.flags = b.flags = 9;
    a.type = fast ? corncob::world::object_type::bullets : corncob::world::object_type::bomb;
    b.type = corncob::world::object_type::fuel_dump;
    b.collision_group = 1;
    bool const expected{input.byte() != 0};
    bool const actual{corncob::game::objects_overlap(a, b)};
    if(actual != expected) {
      throw std::runtime_error{"native collision differs in case " + std::to_string(i)
        + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual)};
    }
  }
  std::cout << count << " native ordinary and fast projectile contacts match" << std::endl;
  return EXIT_SUCCESS;
}

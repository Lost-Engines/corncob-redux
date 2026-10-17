#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include "graphics/ground_detail.h"
#include "tests/reference_reader.h"

int main(int const argc, char const *const argv[]) {
  /// Compare unchanged original generators and all three initialisation loops, including unused records and random continuation
  if(argc != 2) throw std::invalid_argument{"ground-detail fixture required"};
  namespace graphics = corncob::graphics;
  namespace maths = corncob::maths;
  corncob::tests::reference_reader input{argv[1], "CORNGRD1"};
  unsigned int const points{input.dword()}, stars{input.dword()}, banks{input.dword()};
  auto position{[&input]{
    int32_t const x{input.signed_dword()}, y{input.signed_dword()}, z{input.signed_dword()};
    return maths::world_position{x, y, z};
  }};
  for(unsigned int i{0}; i != points; ++i) {
    auto const observer{position()};
    auto point{position()};
    maths::random_source random{input.dword()};
    graphics::regenerate_ground_point(point, observer, random);
    auto const expected{position()};
    auto const seed{input.dword()};
    if(point != expected || random.seed() != seed) throw std::runtime_error{"native ground point " + std::to_string(i)};
  }
  for(unsigned int i{0}; i != stars; ++i) {
    maths::random_source random{input.dword()};
    auto const actual{graphics::generate_star_point(random)};
    auto const expected{position()};
    auto const seed{input.dword()};
    if(actual != expected || random.seed() != seed) throw std::runtime_error{"native star point " + std::to_string(i)};
  }
  for(unsigned int i{0}; i != banks; ++i) {
    auto const observer{position()};
    maths::random_source random{input.dword()};
    graphics::ground_field field;
    field.density(80);
    field.initialise(observer, random);
    for(auto const &point : field.points()) if(point != position()) throw std::runtime_error{"native initial ground bank " + std::to_string(i)};
    for(auto const &star : field.stars()) if(star != position()) throw std::runtime_error{"native initial star bank " + std::to_string(i)};
    auto const seed{input.dword()};
    if(random.seed() != seed) throw std::runtime_error{"native bank initialisation random continuation " + std::to_string(i)};
    field.density(80);
    field.initialise(observer, random);
    if(random.seed() != seed) throw std::runtime_error{"repeated initialisation consumed another bank of random numbers"};
  }
  std::cout << points << " ground points, " << stars << " stars and " << banks << " complete native initialisation passes match" << std::endl;
  return EXIT_SUCCESS;
}

#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include "resources/assets.h"
#include "resources/binary_reader.h"
#include "resources/geometry_bank.h"
#include "tests/reference_reader.h"

int main(int const argc, char const *const argv[]) {
  /// Compare every original animated disc word and verify that shared resource bindings receive those same shapes
  if(argc != 2) throw std::invalid_argument{"particle fixture required"};
  namespace resources = corncob::resources;
  corncob::tests::reference_reader input{argv[1], "CORNPRC1"};
  unsigned int const cases{input.dword()};
  resources::geometry_bank geometry{resources::engine_data_asset()};
  for(unsigned int i{0}; i != cases; ++i) {
    auto particles{geometry.initial_particles()};
    corncob::maths::random_source random{input.dword()};
    particles.advance(random);
    if(particles.colour() != input.byte()) throw std::runtime_error{"native particle colour " + std::to_string(i)};
    for(auto const &disc : particles.discs()) {
      for(unsigned int axis{0}; axis != 3; ++axis) {
        if(disc.position[axis] != input.signed_word()) {
          throw std::runtime_error{"native particle coordinate " + std::to_string(i)};
        }
      }
      if(disc.diameter != input.word()) throw std::runtime_error{"native particle diameter " + std::to_string(i)};
    }
    if(random.seed() != input.dword()) throw std::runtime_error{"native particle random continuation " + std::to_string(i)};
    if(i >= 16) continue;
    geometry.animate_particles(particles);
    resources::binary_reader source{resources::engine_data_asset()};
    unsigned int checked{0};
    for(unsigned int type{0}; type != std::to_underlying(corncob::world::object_type::count); ++type) {
      auto const &definition{geometry.definition(static_cast<corncob::world::object_type>(type))};
      for(auto const model : {definition.models.near, definition.models.middle, definition.models.far}) {
        if(model == 0) continue;
        unsigned int const commands{source.word(model + 6)};
        for(unsigned int octant{0}; octant != 8; ++octant) {
          unsigned int command{0};
          for(unsigned int entry{0}; entry != commands; ++entry) {
            unsigned int const record{model + 8 + (octant * commands + entry) * 4};
            if(source.word(record) == 0) continue;
            auto const &bound{geometry.commands(model, octant)[command++]};
            if(source.word(record) != 6) continue;
            unsigned int const cloud{source.word(record + 2)};
            auto const &shape{std::get<resources::disc_cloud>(geometry.drawing(bound.primitive_index))};
            for(unsigned int disc{0}; disc != shape.discs.size(); ++disc) {
              unsigned int const address{cloud + 2 + disc * 10};
              if(auto const binding{resources::particle_animation::binding(static_cast<uint16_t>(address))}) {
                auto const &expected{particles.discs()[*binding]};
                auto const &actual{shape.discs[disc]};
                if(actual.position != expected.position || actual.diameter != expected.diameter || actual.colour != source.word(address + 8)) {
                  throw std::runtime_error{"animated particle resource binding changed its shape or colour reference"};
                }
                ++checked;
              }
            }
          }
        }
      }
    }
    if(checked == 0) throw std::runtime_error{"particle comparison did not visit any model resource bindings"};
  }
  std::cout << cases << " original shared-colour/35-disc animation and PRNG passes match" << std::endl;
  return EXIT_SUCCESS;
}

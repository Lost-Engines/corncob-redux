#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include "resources/assets.h"
#include "resources/binary_reader.h"
#include "resources/geometry_bank.h"

int main(int const argc, char const *const argv[]) {
  /// Compare the entire changed wire geometry and collision bounds with unchanged original frame updates
  if(argc != 2) throw std::invalid_argument{"model animation fixture required"};
  std::ifstream file{argv[1], std::ios::binary};
  std::vector<uint8_t> raw{std::istreambuf_iterator<char>{file}, {}};
  corncob::resources::binary_reader const data{raw};
  if(raw.size() != 80'908 || std::string{raw.begin(), raw.begin() + 8} != "CORNPUL1") throw std::runtime_error{"invalid animation fixture"};
  namespace resources = corncob::resources;
  namespace world = corncob::world;
  resources::geometry_bank bank{resources::engine_data_asset()};
  unsigned int offset{12};
  auto expect{[&](int16_t const value){
    if(value != data.signed_word(offset)) throw std::runtime_error{"native animated model mismatch at byte " + std::to_string(offset)};
    offset += 2;
  }};
  for(unsigned int i{0}; i != 512; ++i) {
    auto const frame{data.dword(offset)};
    offset += 4;
    auto const pulse{resources::model_animation::at(frame)};
    expect(pulse.wide_wall);
    expect(pulse.small_wall);
    expect(pulse.square_wall);
    expect(static_cast<int16_t>(pulse.upper_saucer));
    expect(static_cast<int16_t>(pulse.lower_saucer));
    bank.animate(frame);
    for(auto const kind : {world::object_type::wire_forcefield_z1, world::object_type::wire_forcefield_x, world::object_type::wire_forcefield_y, world::object_type::wire_forcefield_z}) {
      auto const extents{bank.collision_extents(kind, frame)};
      for(unsigned int axis{0}; axis != 3; ++axis) expect(extents[axis]);
      auto const commands{bank.commands(bank.definition(kind).models.near, 0)};
      auto const &line{std::get<resources::wireframe>(bank.drawing(commands.front().primitive_index)).lines.front()};
      for(auto const &vertex : line.vertices) {
        for(unsigned int axis{0}; axis != 3; ++axis) expect(vertex[axis]);
      }
    }
    unsigned int discs{0};
    for(auto const &command : bank.commands(bank.definition(world::object_type::saucer).models.near, 0)) {
      if(auto const *cloud{std::get_if<resources::disc_cloud>(&bank.drawing(command.primitive_index))}) {
        for(auto const &disc : cloud->discs) {
          auto const diameter{disc.position.z > 0 ? pulse.upper_saucer : pulse.lower_saucer};
          if(disc.diameter != diameter) throw std::runtime_error{"saucer disc mismatch z=" + std::to_string(disc.position.z) + " actual=" + std::to_string(disc.diameter) + " expected=" + std::to_string(diameter)};
          ++discs;
        }
      }
    }
    if(discs != 2) throw std::runtime_error{"saucer lost a pulsing disc"};
  }
  std::cout << "512 original wire-forcefield and saucer geometry/collision passes match" << std::endl;
  return EXIT_SUCCESS;
}

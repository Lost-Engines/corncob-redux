#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include "game/combat.h"
#include "resources/assets.h"
#include "tests/actor_record.h"

int main(int const argc, char const *const argv[]) {
  /// Compare full original hit callbacks, emitted actors, score words and random continuation
  namespace game = corncob::game;
  namespace world = corncob::world;
  if(argc != 2) throw std::invalid_argument{"impact fixture required"};
  std::ifstream file{argv[1], std::ios::binary};
  std::vector<uint8_t> const bytes{std::istreambuf_iterator<char>{file}, {}};
  corncob::resources::binary_reader const data{bytes};
  if(bytes.size() < 12 || std::string{bytes.begin(), bytes.begin() + 8} != "CORNIMP2") throw std::invalid_argument{"invalid impact fixture"};
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset()};
  unsigned int const count{data.dword(8)};
  unsigned int offset{12};
  unsigned int particles{0};
  for(unsigned int i{0}; i != count; ++i) {
    world::theatre theatre;
    game::combat weapons{geometry, theatre, data.dword(offset)};
    weapons.move({0, 0, 0}, {data.signed_word(offset + 4), 5}, false, data.dword(offset));
    weapons.observe_vehicle_site(data.word(offset + 6));
    bool const paired{data.byte(offset + 8) != 0};
    offset += 9;
    theatre.objects[0] = corncob::tests::read_actor(data, offset);
    auto const kind{theatre.objects[0].type};
    theatre.objects[1] = corncob::tests::read_actor(data, offset + 74);
    offset += 148;
    if(paired) {
      weapons.collide(0, 1);
    } else {
      weapons.damage(0, theatre.objects[1]);
    }
    auto const &object{theatre.objects[0]};
    auto const expected{corncob::tests::read_actor(data, offset)};
    auto check{[&](bool const match, std::string const &field){
      if(!match) throw std::runtime_error{"native impact " + std::to_string(i) + " (type " + std::to_string(std::to_underlying(kind)) + "): " + field};
    }};
    try {
      corncob::tests::compare_actor(object, expected, i, "impact");
    }
    catch(std::exception const &error) {
      throw std::runtime_error{std::string{error.what()} + " type " + std::to_string(std::to_underlying(kind))
        + " health " + std::to_string(object.health) + "/" + std::to_string(expected.health)
        + " timer " + std::to_string(object.timer) + "/" + std::to_string(expected.timer)
        + " flags " + std::to_string(object.flags) + "/" + std::to_string(expected.flags)
        + " z " + std::to_string(object.position.z) + "/" + std::to_string(expected.position.z)};
    }
    auto const models{geometry.appearance(object)};
    check(models.near == data.word(offset + 40) && models.middle == data.word(offset + 42) && models.far == data.word(offset + 44), "models");
    offset += 74;
    corncob::tests::compare_actor(theatre.objects[1], corncob::tests::read_actor(data, offset), i, "second contact object");
    auto const second_models{geometry.appearance(theatre.objects[1])};
    check(second_models.near == data.word(offset + 40) && second_models.middle == data.word(offset + 42) && second_models.far == data.word(offset + 44), "second contact models");
    offset += 74;
    auto const score{weapons.stats().encode()};
    for(unsigned int j{0}; j != score.size(); ++j) check(score[j] == data.byte(offset + j), "score byte " + std::to_string(j));
    offset += static_cast<unsigned int>(score.size());
    uint32_t const expected_random{data.dword(offset)};
    offset += 4;
    unsigned int const children{data.word(offset)};
    offset += 2;
    unsigned int actual{0};
    for(unsigned int j{2}; j != theatre.objects.size(); ++j) {
      auto const &child{theatre.objects[j]};
      if(!child.has(world::object_flag::present)) continue;
      check(actual < children, "too many emitted effects");
      corncob::tests::compare_actor(child, corncob::tests::read_actor(data, offset), i, "emitted effect");
      offset += 74;
      ++actual;
    }
    check(actual == children, "emitted effect count " + std::to_string(actual) + "/" + std::to_string(children));
    check(weapons.random_stream().seed() == expected_random, "random continuation");
    particles += actual;
  }
  if(offset != bytes.size()) throw std::runtime_error{"impact fixture length mismatch"};
  std::cout << count << " complete native hit callbacks, " << particles << " emitted effects and their score/random continuation match" << std::endl;
  return EXIT_SUCCESS;
}

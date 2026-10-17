#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include "game/combat.h"
#include "game/scenery.h"
#include "game/session.h"
#include "resources/assets.h"
#include "tests/actor_record.h"

int main(int const argc, char const *const argv[]) {
  /// Compare native local interactions, permanent timers, model switches and objective lockout
  namespace game = corncob::game;
  namespace maths = corncob::maths;
  namespace world = corncob::world;
  if(argc != 2) throw std::invalid_argument{"scenery fixture required"};
  std::ifstream file{argv[1], std::ios::binary};
  std::vector<uint8_t> const bytes{std::istreambuf_iterator<char>{file}, {}};
  corncob::resources::binary_reader const data{bytes};
  if(bytes.size() < 16 || std::string{bytes.begin(), bytes.begin() + 8} != "CORNSCN1") throw std::invalid_argument{"invalid scenery fixture"};
  unsigned int const ranges{data.dword(8)}, handlers{data.dword(12)};
  unsigned int offset{16};
  for(unsigned int i{0}; i != ranges; ++i, offset += 27) {
    maths::world_position const position{data.signed_dword(offset), data.signed_dword(offset + 4), data.signed_dword(offset + 8)};
    maths::world_position const observer{data.signed_dword(offset + 12), data.signed_dword(offset + 16), data.signed_dword(offset + 20)};
    auto const distance{game::interaction_range(position, observer)};
    if(distance.has_value() != (data.byte(offset + 24) != 0) || (distance && *distance != data.word(offset + 25)))
      throw std::runtime_error{"native interaction range " + std::to_string(i)};
  }
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset()};
  for(unsigned int i{0}; i != handlers; ++i) {
    maths::world_position const observer{data.signed_dword(offset), data.signed_dword(offset + 4), data.signed_dword(offset + 8)};
    bool const on_foot{data.byte(offset + 12) != 0};
    auto const pose{data.byte(offset + 13)};
    uint32_t const ticks{data.dword(offset + 14)};
    offset += 18;
    world::theatre theatre;
    game::combat weapons{geometry, theatre};
    auto &object{theatre.objects[0]};
    object = corncob::tests::read_actor(data, offset);
    object.pose = pose == 1 ? world::model_pose::gesture : pose == 2 ? world::model_pose::paved : world::model_pose::normal;
    auto const event{game::advance_scenery(object, observer, on_foot, ticks)};
    if(event == game::scenery_event::contact) weapons.complete(object);
    offset += 74;
    auto const expected{corncob::tests::read_actor(data, offset)};
    corncob::tests::compare_actor(object, expected, i, "scenery");
    auto const models{geometry.appearance(object)};
    if(models.near != data.word(offset + 40) || models.middle != data.word(offset + 42) || models.far != data.word(offset + 44))
      throw std::runtime_error{"native scenery models " + std::to_string(i)};
    offset += 74;
    if((event == game::scenery_event::tower) != (data.byte(offset) != 0) || weapons.stats().objectives_completed != data.word(offset + 1))
      throw std::runtime_error{"native scenery event " + std::to_string(i)};
    offset += 3;
  }
  if(offset != bytes.size()) throw std::runtime_error{"scenery fixture length mismatch"};
  world::theatre distant;
  distant.objects.push_back({
    .position{6'000'000, 6'000'000, 0},
    .attitude{},
    .type{world::object_type::ground_crew},
    .flags{geometry.definition(world::object_type::ground_crew).flags},
    .timer{1234},
  });
  game::session flight{geometry, std::move(distant)};
  flight.advance({});
  if(flight.world().objects[0].timer != 32000) throw std::runtime_error{"distant scenery failed to update"};
  std::cout << ranges << " native local distances and " << handlers << " complete scenery handlers match" << std::endl;
  return EXIT_SUCCESS;
}

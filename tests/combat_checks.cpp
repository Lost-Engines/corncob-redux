#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include "game/combat.h"
#include "resources/assets.h"

namespace {

namespace resources = corncob::resources;
namespace world = corncob::world;
namespace game = corncob::game;
namespace maths = corncob::maths;
namespace flight = corncob::flight;

void require(bool const condition, char const *const message) {
  /// Stop at the first broken player-weapon or destruction transition
  if(!condition) throw std::runtime_error{message};
}

} // anonymous namespace

int main() {
  /// Exercise complete launch/move/contact/damage paths with original templates and real pool ownership

  resources::geometry_bank geometry{resources::engine_data_asset()};
  world::theatre world;
  world.objects.push_back({
    .position{15'000, 0, 720},
    .attitude{},
    .type{world::object_type::fuel_dump},
    .flags{geometry.definition(world::object_type::fuel_dump).flags},
  });
  game::combat combat{geometry, world};
  maths::world_position const position{0, 0, 720};
  maths::angles const attitude{};
  auto const rotation{maths::make_inverse_rotation(attitude)};
  game::weapon_origin const origin{
    .position{position},
    .attitude{attitude},
    .body_to_world{rotation},
    .forward_airflow{0},
    .on_foot{false},
    .remote{false},
    .crashed{false},
  };
  auto pedestrian{origin};
  pedestrian.on_foot = true;
  require(combat.ammunition() == 0, "pilot started with ammunition before leaving an aircraft");
  require(!combat.shoot(pedestrian, false, {}), "unarmed pilot fired a pistol");
  require(!combat.plant_charge(position), "pilot started with an assassin charge before leaving an aircraft");
  for(unsigned int n{0}; n != 13; ++n) {
    require(combat.shoot(origin, false, {}), "gun failed to allocate a bullet");
    for(unsigned int frame{0}; frame != 6; ++frame) combat.advance(position, {});
  }
  require(world.objects.front().type == world::object_type::destroyed_fuel_dump, "guns failed to destroy a fuel dump through native fast contacts");
  require(combat.stats().resource_kills == 1, "fuel destruction credit incorrect");
  auto const orb{combat.spawn(world::object_type::generator_orb, {0, 0, 50000})};
  require(orb.has_value(), "effect pool unexpectedly exhausted");
  world.objects[*orb].flags |= 0x4000;
  world::placed_object const missile{
    .position{0, 0, 0},
    .attitude{},
    .type{world::object_type::missile},
    .flags{0x49},
  };
  combat.damage(*orb, missile);
  require(world.objects[*orb].health == 65, "aircraft-impact-only orb accepted a missile");
  combat.damage(*orb, missile, true);
  require(world.objects[*orb].type == world::object_type::destroyed_generator_orb, "aircraft impact failed to destroy the protected orb");
  combat.complete(world.objects[*orb]);
  require(combat.stats().has(corncob::game::sortie_status::suicide_objective), "suicide objective did not retain its native scoring flag");
  auto const mindbender{combat.spawn(world::object_type::mindbender, {0, 0, 100000})};
  require(mindbender.has_value(), "mindbender allocation failed");
  world.objects[*mindbender].flags |= 0x800;
  combat.damage(*mindbender, missile);
  require(world.objects[*mindbender].health == 1, "mindbender accepted a missile");
  world::placed_object const ordinary_bomb{
    .position{0, 0, 0},
    .attitude{},
    .type{world::object_type::bomb},
    .flags{0xc9},
  };
  combat.damage(*mindbender, ordinary_bomb);
  require(world.objects[*mindbender].health == 1, "special mindbender accepted a plane-dropped bomb");
  world::placed_object charge{ordinary_bomb};
  charge.flags |= 0x800;
  combat.damage(*mindbender, charge);
  require(world.objects[*mindbender].flags == 0, "assassin charge failed against special mindbender");
  flight::aircraft_state aircraft;
  aircraft.position = maths::world_position{0, 0, 50000};
  for(unsigned int n{0}; n != 3; ++n) require(combat.drop_bomb(aircraft, 0, {}), "three-bomb supply depleted too early");
  require(!combat.drop_bomb(aircraft, 0, {}), "fourth bomb bypassed the supply limit");
  combat.fire({}, origin, 540 * 64, {});
  require(combat.bomb_supply() == 1, "bomb replacement timer failed");
  combat.rearm();
  require(combat.ammunition() == 0 && !combat.plant_charge(position), "boarding supplied the person's weapons");
  combat.rearm_person();
  require(combat.ammunition() == 20, "leaving an aircraft did not supply twenty pistol rounds");
  require(combat.plant_charge({0, 0, 720}), "carried charge unavailable");
  require(!combat.plant_charge({0, 0, 720}), "carried charge duplicated");
  combat.arm_charges();
  bool armed{false};
  for(auto const &object : world.objects) if(object.type == world::object_type::bomb && object.has(world::object_flag::special))
    armed = (object.flags & 4u) == 0 && object.collision_extents.x == 6000;
  require(armed, "D failed to release and arm the planted charge");
  for(unsigned int n{0}; n != 20; ++n) require(combat.shoot({position, attitude, rotation, 0, true, false, false}, false, {}), "pistol supply depleted early");
  require(!combat.shoot({position, attitude, rotation, 0, true, false, false}, false, {}), "pistol supplied more than twenty rounds");
  std::cout << "Player guns, fast contacts, destruction, protected targets, bomb recharge and planted charges exercised" << std::endl;
  return EXIT_SUCCESS;
}

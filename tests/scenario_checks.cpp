#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include "game/actors.h"
#include "game/session.h"
#include "resources/assets.h"

namespace {

void require(bool const condition, char const *const message) {
  /// Stop the scenario at the first broken cross-system rule
  if(!condition) throw std::runtime_error{message};
}

} // anonymous namespace

int main() {
  /// Exercise delayed objectives, protected orbs, nearby spawning and actual hostile shell launches
  namespace world = corncob::world;
  namespace maths = corncob::maths;
  namespace game = corncob::game;
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset()};
  world::theatre theatre;
  auto add{[&](world::object_type const type, maths::world_position const &position, uint16_t const extra = 0){
    auto const &definition{geometry.definition(type)};
    theatre.objects.push_back({
      .position{maths::world_position{position}},
      .attitude{},
      .type{type},
      .flags{static_cast<uint16_t>(definition.flags | extra)},
      .timer{definition.initial_timer},
    });
  }};
  add(world::object_type::generator_orb, {0, 0, 6000});
  add(world::object_type::fuel_dump, {50'000, 0, 0});
  add(world::object_type::kla_site, {100'000, 0, 0});
  add(world::object_type::aaa_site, {0, 100'000, 0}, 0x7000);
  game::combat weapons{geometry, theatre};
  game::actors actors{geometry, theatre, weapons};
  maths::world_position const observer{0, 0, 5000};
  maths::fixed_vector const velocity{1000, 0, 0};
  game::actor_context const context{
    .observer{observer},
    .target{observer},
    .target_velocity{velocity},
    .on_foot{false},
    .timer{0},
  };
  actors.advance(context, {});
  require(weapons.stats().objectives_completed == 0, "intact destroy-me orb completed itself");
  world::placed_object const bomb{
    .position{0, 0, 0},
    .attitude{},
    .type{world::object_type::bomb},
    .flags{0xc9},
  };
  weapons.damage(0, bomb, true);
  actors.advance(context, {});
  require(weapons.stats().objectives_completed == 1, "destroyed orb lost its objective handler");
  actors.advance(context, {});
  require(weapons.stats().objectives_completed == 1, "completed orb repeatedly awarded credit");
  theatre.objects[2].timer = 1;
  actors.advance(context, {});
  require(std::ranges::any_of(theatre.objects, [](auto const &o){return o.type == world::object_type::kla && o.has(world::object_flag::present);}), "local KLA launcher failed to spawn");
  bool shell_seen{false};
  for(unsigned int frame{0}; frame != 300; ++frame) {
    weapons.move(observer, {});
    actors.advance(context, {});
    shell_seen = shell_seen || std::ranges::any_of(theatre.objects, [](auto const &o){return o.type == world::object_type::aa_shell && o.has(world::object_flag::present);});
    weapons.collisions(observer);
  }
  require(shell_seen, "highest-quality AAA never fired");
  require(world::in_active_neighbourhood({0, 0, 0}, observer), "observer's tile was not active");
  require(!world::in_active_neighbourhood({6'000'000, 6'000'000, 0}, observer), "distant theatre tile remained active");
  // A newly visited clear-fuel orb must see its local fuel before awarding a goal.
  world::theatre targets;
  auto const &orb_definition{geometry.definition(world::object_type::generator_orb)};
  auto const &fuel_definition{geometry.definition(world::object_type::fuel_dump)};
  targets.objects.push_back({
    .position{0, 0, 10'000},
    .attitude{},
    .type{world::object_type::generator_orb},
    .flags{static_cast<uint16_t>(orb_definition.flags | 0x1800u)},
    .timer{32'000},
  });
  targets.objects.push_back({
    .position{10'000, 0, 0},
    .attitude{},
    .type{world::object_type::fuel_dump},
    .flags{fuel_definition.flags},
    .timer{32'000},
  });
  targets.objects.push_back({
    .position{1000, 0, 0},
    .attitude{},
    .type{world::object_type::mortar},
    .flags{geometry.definition(world::object_type::mortar).flags},
    .timer{9995},
  });
  targets.objects.push_back({
    .position{5000, 0, 0},
    .attitude{},
    .type{world::object_type::shell},
    .flags{geometry.definition(world::object_type::shell).flags},
    .timer{32'000},
  });
  game::combat practice_weapons{geometry, targets};
  game::actors practice_actors{geometry, targets, practice_weapons};
  maths::world_position const exposed{5000, 0, 8000};
  auto const &effects{practice_actors.advance({exposed, exposed, velocity, false, 0}, {})};
  require(practice_weapons.stats().objectives_completed == 0, "unprimed local census completed an uncleared objective");
  require(effects.attraction.x < 0 && effects.attraction.z > 0 && effects.orb_distortion != 0, "special orb failed to attract and distort the aircraft view");
  require(std::ranges::count_if(targets.objects, [](auto const &object){
    return object.has(world::object_flag::present) && object.type == world::object_type::flotsam_2 && object.collision_group == 3;
  }) == 2,
    "mortar failed to launch its two dangerous fragments");
  practice_weapons.damage(3, bomb);
  require(!targets.objects[3].has(world::object_flag::present) && std::ranges::any_of(targets.objects, [](auto const &object){
    return object.has(world::object_flag::present) && object.type == world::object_type::vehicle_site && object.quality() == 7;
  }), "concealed site did not reveal its configured contents when struck");
  auto objective_world{[&](world::object_type const target_type, uint16_t const objective_quality, uint16_t const target_quality){
    world::theatre result;
    result.objects.push_back({
      .position{0, 0, 10'000},
      .attitude{},
      .type{world::object_type::generator_orb},
      .flags{static_cast<uint16_t>(orb_definition.flags | objective_quality << 12)},
      .timer{32'000},
    });
    result.objects.push_back({
      .position{10'000, 0, 0},
      .attitude{},
      .type{target_type},
      .flags{static_cast<uint16_t>(geometry.definition(target_type).flags | target_quality << 12 | 8u)},
      .timer{32'000},
    });
    return result;
  }};
  auto hidden_fuel{objective_world(world::object_type::shell, 1, 6)};
  game::combat hidden_weapons{geometry, hidden_fuel};
  game::actors hidden_actors{geometry, hidden_fuel, hidden_weapons};
  for(unsigned int frame{0}; frame != 2; ++frame) hidden_actors.advance(context, {});
  require(hidden_weapons.stats().objectives_completed == 0, "concealed fuel failed to block a clear-fuel objective");
  hidden_weapons.damage(1, bomb);
  auto const revealed{std::ranges::find_if(hidden_fuel.objects, [](auto const &object){
    return object.type == world::object_type::fuel_dump && object.has(world::object_flag::present);
  })};
  require(revealed != hidden_fuel.objects.end(), "concealed fuel did not reveal its fuel dump");
  hidden_actors.advance(context, {});
  hidden_weapons.damage(static_cast<unsigned int>(revealed - hidden_fuel.objects.begin()), bomb, true);
  for(unsigned int frame{0}; frame != 2; ++frame) hidden_actors.advance(context, {});
  require(hidden_weapons.stats().objectives_completed == 1, "destroying the revealed fuel failed to complete its objective");
  auto alien_target{objective_world(world::object_type::alien, 7, 0)};
  game::combat alien_weapons{geometry, alien_target};
  game::actors alien_actors{geometry, alien_target, alien_weapons};
  alien_actors.advance(context, {});
  world::placed_object const bullet{
    .position{0, 0, 0},
    .attitude{},
    .type{world::object_type::bullets},
    .flags{0x89},
  };
  alien_weapons.damage(1, bullet);
  for(unsigned int frame{0}; frame != 2; ++frame) alien_actors.advance(context, {});
  require(alien_weapons.stats().objectives_completed == 1 && alien_target.objects[1].has(world::object_flag::present),
    "a visible alien corpse blocked the cleared-alien objective");
  for(auto const type : {world::object_type::ground_vehicle, world::object_type::rescue_van}) {
    auto wrecked_vehicle{objective_world(type, 3, 0)};
    game::combat wreck_weapons{geometry, wrecked_vehicle};
    game::actors wreck_actors{geometry, wrecked_vehicle, wreck_weapons};
    for(unsigned int frame{0}; frame != 2; ++frame) wreck_actors.advance(context, {});
    require(wreck_weapons.stats().objectives_completed == 0, "a living vehicle failed to block its objective");
    wreck_weapons.damage(1, bomb, true);
    for(unsigned int frame{0}; frame != 2; ++frame) wreck_actors.advance(context, {});
    require(wreck_weapons.stats().objectives_completed == 1 && wrecked_vehicle.objects[1].has(world::object_flag::present),
      "a visible overturned vehicle blocked the cleared-vehicle objective");
  }
  world::theatre barriers;
  auto const &barrier_definition{geometry.definition(world::object_type::forcefield_x)};
  barriers.objects.push_back({
    .position{0, 0, 8000},
    .attitude{},
    .type{world::object_type::forcefield_x},
    .flags{barrier_definition.flags},
    .timer{32'000},
  });
  game::combat barrier_weapons{geometry, barriers};
  game::actors barrier_actors{geometry, barriers, barrier_weapons};
  for(unsigned int frame{0}; frame != 10; ++frame) barrier_actors.advance({observer, observer, velocity, false, 0}, {});
  require(geometry.appearance(barriers.objects[0]).far == 0 && barriers.objects[0].has(world::object_flag::present), "distant blinking wall did not retain its near barrier");
  barrier_weapons.damage(0, bomb);
  require(geometry.appearance(barriers.objects[0]).far == barrier_definition.models.near && barriers.objects[0].health == barrier_definition.hit_points, "barrier contact damaged the field instead of briefly revealing it");
  game::session rescue{geometry, world::theatre{}};
  rescue.press(game::action::call_rescue);
  rescue.press(game::action::call_rescue);
  require(std::ranges::count_if(rescue.world().objects, [](auto const &object){
    return object.type == world::object_type::rescue_van && object.has(world::object_flag::present);
  }) == 1,
    "rescue call failed or created duplicate vans");
  for(unsigned int frame{0}; frame != 200 && !rescue.finished(); ++frame) {
    auto const &person{rescue.pilot()};
    auto const desired{maths::arctangent(maths::word(-person.position.x), maths::word(-person.position.y))};
    int const error{maths::word(desired - person.attitude.heading)};
    rescue.advance({
      .stick{
        .roll{static_cast<int16_t>(-std::clamp(error, -512, 512))},
      },
      .walk_forward{std::abs(error) < 2048},
    });
  }
  require(rescue.player() == game::player_mode::rescued && rescue.results().has(game::sortie_status::rescued), "called van did not rescue a pilot who walked up to it");
  world::theatre base;
  base.start = maths::world_position{0, 3000, 720};
  base.objects.push_back({
    .position{0, 0, 720},
    .attitude{},
    .type{world::object_type::aircraft},
    .flags{static_cast<uint16_t>(geometry.definition(world::object_type::aircraft).flags | 0x104u)},
    .timer{32'000},
  });
  base.objects.push_back({
    .position{10'000, 0, 0},
    .attitude{},
    .type{world::object_type::tower_doorway},
    .flags{geometry.definition(world::object_type::tower_doorway).flags},
    .timer{32'000},
  });
  game::session tower{geometry, std::move(base)};
  tower.press(game::action::toggle_messages);
  tower.advance({});
  require(tower.player() == game::player_mode::in_aircraft && tower.aircraft_damage().maximum_throttle == 0, "tower scenario did not start with the crashed aircraft");
  tower.press(game::action::eject);
  bool entered_tower{false};
  for(unsigned int frame{0}; frame != 1000 && !entered_tower; ++frame) {
    auto const &person{tower.pilot()};
    auto const desired{maths::arctangent(maths::word(10'000 - person.position.x), maths::word(-person.position.y))};
    int const error{maths::word(desired - person.attitude.heading)};
    tower.advance({
      .stick{
        .roll{static_cast<int16_t>(-std::clamp(error, -512, 512))},
      },
      .walk_forward{std::abs(error) < 2048},
    });
    entered_tower = tower.take_tower();
  }
  require(entered_tower && !tower.finished() && !tower.take_tower(), "tower doorway ended the sortie or repeated its intelligence request");
  auto const entry_timer{tower.timer()};
  auto const entry_y{tower.pilot().position.y};
  tower.advance({});
  require(tower.timer() == entry_timer, "tower visit advanced the retained flight");
  tower.resume_from_tower();
  require(tower.pilot().position.y == entry_y - 2000 && tower.pilot().attitude.heading == static_cast<maths::angle>(-16'384), "tower exit did not place the pilot outside its doorway");
  require(tower.aircraft_damage().maximum_throttle == 200 && tower.craft().rocket_ticks == 2184, "tower visit failed to repair and recharge the retained aircraft");
  tower.advance({});
  require(!tower.finished() && !tower.take_tower(), "resuming from tower immediately reentered intelligence");
  world::theatre protected_base;
  protected_base.start = maths::world_position{0, 3000, 720};
  protected_base.objects.push_back({
    .position{0, 0, 720},
    .attitude{
      .pitch{16'384},
      .roll{16'384},
    },
    .type{world::object_type::aircraft},
    .flags{geometry.definition(world::object_type::aircraft).flags},
    .timer{32'000},
  });
  game::session protected_flight{geometry, std::move(protected_base)};
  protected_flight.set_invincible(true);
  protected_flight.advance({});
  protected_flight.press(game::action::throttle_up);
  protected_flight.advance({});
  require(!protected_flight.finished() && protected_flight.player() == game::player_mode::in_aircraft, "invulnerable aircraft died on a fatal ground-contact attitude");
  require(protected_flight.craft().attitude.pitch == 0 && protected_flight.craft().attitude.roll == 0 && protected_flight.results().aircraft_lost == 0,
    "invulnerable recovery failed to level the retained aircraft or counted a loss");
  game::session warnings{geometry, world::theatre{}};
  require(!warnings.request_exit() && warnings.take_radio() == corncob::resources::radio_notice::leave_on_foot, "first Escape away from base did not show the original walking warning");
  require(warnings.request_exit() && !warnings.take_radio(), "second Escape repeated its warning instead of permitting departure");
  std::cout << "Local spawning, hostile shells, persistent orb objectives and four-tile activation exercised" << std::endl;
  return EXIT_SUCCESS;
}

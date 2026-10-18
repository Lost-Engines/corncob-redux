#include "combat.h"
#include <algorithm>
#include <cstdlib>
#include <utility>
#include "maths/integer.h"
#include "resources/actor_tables.h"
#include "world/motion.h"

namespace corncob::game {

namespace {

uint16_t flag(world::object_flag const value) noexcept {
  /// Keep common live flags separate from overloaded type-specific bits
  return std::to_underlying(value);
}
std::optional<maths::fixed_vector> separation(maths::world_position const &a, maths::world_position const &b) noexcept {
  /// GETX1 accepts only relative components strictly inside the signed word range
  maths::fixed_vector result{0, 0, 0};
  for(unsigned int axis{0}; axis != 3; ++axis) {
    auto const difference{maths::dword(static_cast<int64_t>(a[axis]) - b[axis])};
    if(std::abs(static_cast<int64_t>(difference)) >= 32'768) return std::nullopt;
    result[axis] = static_cast<int16_t>(difference);
  }
  return result;
}
void offset(world::placed_object &object, maths::rotation const &rotation, maths::fixed_vector const &local) {
  /// Launch positions are transformed in the aircraft's or person's local axes
  auto const delta{maths::transform(rotation, local)};
  for(unsigned int axis{0}; axis != 3; ++axis) object.position[axis] = maths::dword(static_cast<int64_t>(object.position[axis]) + delta[axis]);
}
uint16_t lifetime(unsigned int const frames, flight::time_step const step) noexcept {
  /// UWTADJ compensates initial frame lifetimes for the current physical time step
  return static_cast<uint16_t>(frames * static_cast<unsigned int>(step.normal) / static_cast<unsigned int>(step.elapsed));
}

} // anonymous namespace

bool objects_overlap(world::placed_object const &a, world::placed_object const &b) noexcept {
  /// Retain box contacts and the original asymmetric spherical and fast-projectile predicates
  if((a.type == world::object_type::crater && b.type == world::object_type::aircraft) || (b.type == world::object_type::crater && a.type == world::object_type::aircraft)) return false;
  if(a.collision_group == b.collision_group || !a.has(world::object_flag::present) || !b.has(world::object_flag::present)) return false;
  if(((a.flags | b.flags) & flag(world::object_flag::box_collision)) != 0)
    return world::boxes_overlap(a.position, a.collision_extents, b.position, b.collision_extents);
  if(((a.flags | b.flags) & flag(world::object_flag::collision_callback)) == 0) return false;
  auto const delta{separation(a.position, b.position)};
  if(!delta) return false;
  uint32_t const distance{maths::magnitude(*delta)};
  uint32_t const ar{static_cast<uint16_t>(a.collision_extents.x)}, br{static_cast<uint16_t>(b.collision_extents.x)};
  if(a.type != world::object_type::bullets && a.type != world::object_type::missile)
    return ar != 0 && br != 0 && distance < (br >= ar ? br : br + ar);
  if(!b.has(world::object_flag::collision_callback)) return false;
  if(std::max(ar, br) >= distance) return true;
  uint32_t const speed{maths::magnitude(a.velocity)};
  int32_t dot{0};
  for(unsigned int axis{0}; axis != 3; ++axis) dot = maths::dword(static_cast<int64_t>(dot) + static_cast<int32_t>((*delta)[axis]) * a.velocity[axis]);
  if(dot >= 0) return false;
  int64_t const cosine{(-static_cast<int64_t>(dot) << 16) / (distance * speed)};
  return cosine >= 45'000 && (distance >> 1) <= speed;
}

combat::combat(resources::geometry_bank const &geometry, world::theatre &theatre, uint32_t const seed) :
  geometry{geometry},
  theatre{theatre},
  random{seed},
  scenery_count{static_cast<unsigned int>(theatre.objects.size())} {
  /// Reserve the original independent effect and bullet pools without ever moving live object indices
  theatre.objects.resize(scenery_count + effect_capacity + bullet_capacity);
  for(unsigned int i{0}; i != scenery_count; ++i) {
    auto &object{theatre.objects[i]};
    auto const &definition{geometry.definition(object.type)};
    object.health = definition.hit_points;
    object.collision_group = definition.collision_group;
    object.motion = definition.motion;
    object.collision_extents = maths::fixed_vector{definition.collision_extents};
  }
}
std::optional<unsigned int> combat::allocate(bool const bullet) {
  /// FINDExp and FINDBUL wrap independent fixed-capacity cursors and fail cleanly when full
  auto &cursor{bullet ? bullet_cursor : effect_cursor};
  unsigned int const count{bullet ? bullet_capacity : effect_capacity};
  unsigned int const begin{scenery_count + (bullet ? effect_capacity : 0)};
  for(unsigned int n{0}; n != count; ++n) {
    unsigned int const index{begin + cursor};
    cursor = (cursor + 1) % count;
    if(theatre.objects[index].flags == 0) return index;
  }
  return std::nullopt;
}
std::optional<unsigned int> combat::spawn(world::object_type const type, maths::world_position const &position) {
  /// Expand a template into an owned effect slot; CCT scenery remains outside the expiring pool
  auto const index{allocate()};
  if(!index) return std::nullopt;
  auto const &definition{geometry.definition(type)};
  theatre.objects[*index] = {
    .position{maths::world_position{position}},
    .attitude{definition.initial_attitude},
    .velocity{maths::fixed_vector{definition.initial_velocity}},
    .type{type},
    .flags{definition.flags},
    .timer{definition.initial_timer},
    .health{definition.hit_points},
    .collision_group{definition.collision_group},
    .motion{definition.motion},
    .collision_extents{maths::fixed_vector{definition.collision_extents}},
  };
  return index;
}
void combat::explosion(maths::world_position const &position, uint16_t const damaging_flags) {
  /// STRTBOOM uses a five-frame white/yellow/red model sequence with cause-specific attack flags
  if(auto const index{spawn(world::object_type::explosion, position)}) {
    auto &object{theatre.objects[*index]};
    ++cues.explosions;
    object.velocity.x = maths::word(object.velocity.x + random.below(100) - 50);
    object.velocity.y = maths::word(object.velocity.y + random.below(100) - 50);
    object.velocity.z = maths::word(object.velocity.z + random.below(450));
    object.timer = 5;
    object.flags |= static_cast<uint16_t>(0x8000u | damaging_flags);
  }
}
bool combat::shoot(weapon_origin const &origin, bool const cannon, flight::time_step const step) {
  /// BULSTRT alternates wing muzzles; the finite pistol shares the pool and uses the person's orientation
  if(origin.on_foot && !origin.remote) {
    muzzle_offset = 0;
    gun_average = 33;
    if(pistol_rounds == 0) return false;
    --pistol_rounds;
  } else if(muzzle_offset == 0) muzzle_offset = 720;
  auto const index{allocate(true)};
  if(!index) return false;
  auto const &definition{geometry.definition(world::object_type::bullets)};
  auto &object{theatre.objects[*index]};
  object = {
    .position{maths::world_position{origin.position}},
    .attitude{origin.attitude},
    .velocity{maths::transform(origin.body_to_world, {maths::word(origin.forward_airflow + (cannon ? 700 : 4400)), 0, 0})},
    .type{world::object_type::bullets},
    .flags{static_cast<uint16_t>(origin.on_foot && origin.remote ? 1 : 9)},
    .timer{lifetime(cannon ? 48 : 96, step)},
    .collision_group{definition.collision_group},
    .collision_extents{maths::fixed_vector{definition.collision_extents}},
  };
  offset(object, origin.body_to_world, {0, muzzle_offset, 0});
  muzzle_offset = maths::negate(muzzle_offset);
  ++cues.gunshots;
  gun_average += 2;
  return true;
}
bool combat::launch_missile(weapon_origin const &origin, flight::time_step const /*step*/) {
  /// STRTMIS launches unguided along the current forward airflow, then its early burn accelerates that velocity
  if(origin.crashed || (origin.on_foot && !origin.remote)) return false;
  auto const index{spawn(world::object_type::missile, origin.position)};
  if(!index) return false;
  auto &object{theatre.objects[*index]};
  object.attitude = origin.attitude;
  object.velocity = maths::transform(origin.body_to_world, {maths::word(origin.forward_airflow + 125), 0, 0});
  object.timer = 100;
  offset(object, origin.body_to_world, {0, missile_offset, 0});
  missile_offset = maths::negate(missile_offset);
  ++cues.missiles;
  missile_average += 5;
  return true;
}
bool combat::drop_bomb(flight::aircraft_state &aircraft, uint32_t const tick, flight::time_step const /*step*/) {
  /// STRTBOMB copies the aircraft pose and preserves the original word-by-word recoil writes
  if(bombs == 0) return false;
  auto const index{spawn(world::object_type::bomb, aircraft.position)};
  if(!index) return false;
  auto &object{theatre.objects[*index]};
  object.attitude = aircraft.attitude;
  object.flags &= static_cast<uint16_t>(~0xe8u);
  object.timer = 3200;
  std::array<int16_t*, 6> const values{&aircraft.velocity.x, &aircraft.velocity.y, &aircraft.velocity.z,
    &aircraft.body_velocity.x, &aircraft.body_velocity.y, &aircraft.body_velocity.z};
  for(unsigned int axis{0}; axis != 3; ++axis) {
    object.velocity[axis] = static_cast<int16_t>(*values[axis] >> 4);
    unsigned int const recoil{axis * 2};
    bool const borrow{static_cast<uint16_t>(*values[recoil]) < static_cast<uint16_t>(object.velocity[axis])};
    *values[recoil] = maths::word(*values[recoil] - object.velocity[axis]);
    *values[recoil + 1] = maths::word(*values[recoil + 1] - borrow);
  }
  offset(object, aircraft.body_to_world, {-500, 0, -500});
  --bombs;
  bomb_replacement_tick = tick;
  return true;
}
bool combat::plant_charge(maths::world_position const &position) {
  /// The one carried assassin charge is stationary and harmless until D releases it
  if(!charge_available) return false;
  auto const index{spawn(world::object_type::bomb, position)};
  if(!index) return false;
  auto &object{theatre.objects[*index]};
  object.flags = static_cast<uint16_t>((object.flags & ~8u) | 0x804u);
  object.attitude.pitch = static_cast<maths::angle>(-16'384);
  object.velocity = maths::fixed_vector{0, 0, 0};
  object.collision_extents.x = 0;
  object.collision_group = 0;
  object.timer = 3200;
  charge_available = false;
  return true;
}
void combat::arm_charges() {
  /// D enlarges the planted charge's collision radius and allows it to fall into a target
  for(auto &object : theatre.objects) {
    if(object.type == world::object_type::bomb && object.has(world::object_flag::present) && (object.flags & 4u) != 0) {
      object.flags = static_cast<uint16_t>((object.flags | 0xe8u) & ~4u);
      object.collision_extents.x = 6000;
    }
  }
}
void combat::fire(weapon_controls const &controls, weapon_origin const &origin, uint32_t const tick, flight::time_step const step) {
  /// Recovery and fire delays follow the original BIOS-rate counters independently of frame count
  if(tick >= gun_ready_tick) {
    gun_ready_tick = (tick / 64 + (controls.cannon ? 1 : 2)) * 64;
    if(gun_average != 0) --gun_average;
    if(missile_average != 0) --missile_average;
    if((controls.gun || controls.cannon) && gun_average <= 32) shoot(origin, controls.cannon, step);
  }
  if(tick >= missile_ready_tick) {
    missile_ready_tick = (tick / 64 + 2) * 64;
    if(controls.missile && missile_average <= 16) launch_missile(origin, step);
  }
  if(bombs < bomb_capacity && tick - bomb_replacement_tick >= 540u * 64u * static_cast<unsigned int>(step.normal) / static_cast<unsigned int>(step.elapsed)) {
    ++bombs;
    bomb_replacement_tick = tick;
  }
}

maths::random_source &combat::random_stream() noexcept {
  /// Flight damage, active actors and effect constructors consume the same original gameplay stream
  return random;
}
void combat::observe_vehicle_site(uint16_t const rest) noexcept {
  /// VESTTIM leaves the last visited quality's rest interval for its subsequent collision reset
  vehicle_site_rest = rest;
}
vehicle_effects combat::take_vehicle_effects() {
  /// Contacts discovered during collision processing feed the next retained actor pass
  return std::exchange(hit_vehicle_effects, {});
}
void combat::impact_sound() noexcept {
  /// BIGBOOM requests impact audio without creating an additional geometric explosion
  ++cues.explosions;
}
unsigned int combat::scatter(world::placed_object const &source, unsigned int const requested, fragment_pattern const pattern, flight::time_step const step, uint8_t const collision_group) {
  /// Emit original fragments and report template-selection attempts, retaining the scratch-pointer side effect needed by paired contacts
  bool const incendiary{pattern == fragment_pattern::incendiary}, mortar{pattern == fragment_pattern::mortar || collision_group == 3};
  bool const outward{pattern == fragment_pattern::outward};
  unsigned int const count{incendiary || mortar ? requested : random.below(static_cast<uint16_t>(requested / 2)) + requested / 2 + 1};
  unsigned int const present{static_cast<unsigned int>(std::ranges::count_if(theatre.objects, [](auto const &object){
    return object.has(world::object_flag::present) && (object.type == world::object_type::flotsam_1 || object.type == world::object_type::flotsam_2);
  }))};
  if(!incendiary && present + count >= 25) return 0;
  unsigned int attempted{0};
  for(unsigned int n{0}; n != count; ++n) {
    world::object_type type{world::object_type::incendiary_fragment};
    if(!incendiary) {
      // the native routine randomizes a template velocity before overwriting it; consume that draw too
      (void)random.below(4000);
      type = mortar || (random.next() & 0x8000u) == 0 ? world::object_type::flotsam_2 : world::object_type::flotsam_1;
    }
    ++attempted;
    auto const index{spawn(type, source.position)};
    if(!index) return attempted;
    auto &object{theatre.objects[*index]};
    // ADD touches only the altitude's low word; it deliberately does not propagate a carry
    object.position.z = maths::dword((static_cast<uint32_t>(source.position.z) & 0xffff0000u) | static_cast<uint16_t>(source.position.z + 750));
    object.flags |= 0x8000u;
    if(incendiary) {
      object.velocity = maths::fixed_vector{maths::word(random.below(300) - 150), maths::word(random.below(300) - 150),
        static_cast<int16_t>(random.below(1200))};
      object.timer = lifetime(150, step);
      object.motion.spin = maths::fixed_vector{static_cast<int16_t>(random.below(4000)), static_cast<int16_t>(random.below(4000)),
        static_cast<int16_t>(random.below(4000))};
    } else {
      object.velocity = maths::fixed_vector{source.velocity};
      object.velocity.x = maths::word(object.velocity.x + random.below(1500) - 750);
      object.velocity.y = maths::word(object.velocity.y + random.below(1500) - 750);
      object.velocity.z = maths::word(object.velocity.z + random.below(outward ? 1500 : 5000) - (outward ? 750 : 0));
      auto const maximum{lifetime(outward ? 18 : 180, step)};
      object.timer = static_cast<uint16_t>(random.below(maximum) + maximum / 4);
      object.collision_group = mortar ? 3 : collision_group;
      if(mortar) {
        object.flags |= 0x30u;
        object.motion.gravity = maths::word(static_cast<uint16_t>(object.motion.gravity) | 0x100u);
      }
    }
  }
  return attempted;
}

void combat::shatter(world::placed_object const &source, unsigned int const requested, bool const outward) {
  /// STRTSHARD leaves spinning fragments on the ground for 6,200 frames, independently of the short-lived flotsam cap
  unsigned int const count{random.below(static_cast<uint16_t>(requested / 2)) + requested / 2 + 1};
  for(unsigned int n{0}; n != count; ++n) {
    auto const type{(random.next() & 0x8000u) == 0 ? world::object_type::shard_2 : world::object_type::shard};
    auto const index{spawn(type, source.position)};
    if(!index) return;
    auto &object{theatre.objects[*index]};
    object.position.z = maths::dword((static_cast<uint32_t>(source.position.z) & 0xffff0000u) | static_cast<uint16_t>(source.position.z + 750));
    object.velocity = maths::fixed_vector{source.velocity};
    object.velocity.x = maths::word(object.velocity.x + random.below(375) - 187);
    object.velocity.y = maths::word(object.velocity.y + random.below(375) - 187);
    object.velocity.z = maths::word(object.velocity.z + random.below(1500) - (outward ? 750 : 0));
    object.timer = 6200;
    object.flags |= 0x8000u;
    object.motion.spin = maths::fixed_vector{static_cast<int16_t>(random.below(4000)), static_cast<int16_t>(random.below(4000)), static_cast<int16_t>(random.below(4000))};
  }
}

void combat::detonate(unsigned int const index) {
  /// Bombs leave low-altitude craters and incendiaries; missiles produce a visual blast and three debris pieces
  auto &object{theatre.objects[index]};
  auto const source{object};
  if(source.type == world::object_type::bomb) {
    explosion(source.position, 0x60);
    scatter(source, (source.flags & 2u) != 0 ? 5 : 15, fragment_pattern::incendiary, tick_step);
    object.flags = 0;
    if(source.position.z >= 0 && source.position.z <= 10'000) {
      object = source;
      object.type = world::object_type::crater;
      object.position.z = 10;
      object.attitude = {};
      uint16_t const full_lifetime{lifetime(3000, tick_step)};
      crater_arm_timer = static_cast<uint16_t>(full_lifetime - 100u);
      object.timer = (source.flags & 2u) != 0 ? static_cast<uint16_t>(full_lifetime >> 4) : full_lifetime;
      object.flags = source.flags & 0x911u;
      object.pose = world::model_pose::blast_crater;
    }
  } else {
    explosion(source.position, 0x20);
    scatter(source, source.type == world::object_type::aa_shell ? 9 : 3, fragment_pattern::outward, tick_step);
    object.flags = 0;
  }
}
void combat::detonate_object(unsigned int const index) {
  /// Timed actor handlers share the same detonation path as direct collision callbacks
  detonate(index);
}
void combat::complete(world::placed_object &object) {
  /// INCONDEST awards an objective once, leaving already-complete anchors untouched
  if(object.has(world::object_flag::objective_complete)) return;
  object.flags |= flag(world::object_flag::objective_complete);
  ++statistics.objectives_completed;
  if(object.type == world::object_type::destroyed_generator_orb && object.quality() == 4) statistics.mark(sortie_status::suicide_objective);
}
contact_continuation combat::damage(unsigned int const index, world::placed_object const &attacker, bool const aircraft_impact) {
  /// Type handlers retain their weapon restrictions and wreck states before applying common hit-point damage
  using enum contact_continuation;
  auto &object{theatre.objects.at(index)};
  if((geometry.definition(object.type).properties & std::to_underlying(world::object_property::forcefield)) != 0) {
    if(object.type == world::object_type::forcefield_x || object.type == world::object_type::forcefield_y
      || object.type == world::object_type::forcefield_z || object.type == world::object_type::forcefield_z1) {
      object.timer = 32'000;
      object.pose = world::model_pose::revealed_forcefield;
    }
    return original_partner;
  }
  if(object.type == world::object_type::barrage_balloon || object.type == world::object_type::bee_swarm) {
    object.timer = 32'000;
    return original_partner;
  }
  if(!object.has(world::object_flag::collision_callback)) return original_partner;
  using enum world::object_type;
  // Slalom changes these collision handlers as well as their geometry: trees are inert; BMRTIM calls GMDEST for the farm.
  if(theatre.slalom && (object.type == shell || object.type == mortar)) return original_partner;
  world::object_type const damage_handler{theatre.slalom && object.type == saucer ? fuel_dump : object.type};
  if(object.type == radar_dish) object.attitude.heading = static_cast<maths::angle>(current_tick * 4u);
  if(object.type == bomb || object.type == missile || object.type == aa_shell) {
    detonate(index);
    return original_partner;
  }
  if(object.type == bullets) {
    scatter(object, 2, fragment_pattern::outward, tick_step, attacker.collision_group & 3u);
    object.flags = 0;
    return original_partner;
  }
  if(object.type == tower_doorway) return original_partner;
  if(object.type == flotsam_1 || object.type == flotsam_2) {
    if(object.collision_group == 3) {
      this->explosion(object.position, 0x20);
      object.flags = 0;
    }
    return original_partner;
  }
  if(object.type == shell) {
    auto const source{object};
    auto const type{static_cast<world::object_type>(resources::shell_contents_qualities[object.quality()].spawn_type)};
    if(auto const spawned{spawn(type, source.position)}) {
      auto &replacement{theatre.objects[*spawned]};
      replacement.flags |= 0xf000u;
      replacement.attitude = source.attitude;
      replacement.position.z = maths::dword((static_cast<uint32_t>(source.position.z) & 0xffff0000u) | static_cast<uint16_t>(geometry.definition(type).initial_altitude));
    }
    object.flags = 0;
    return original_partner;
  }
  if(object.type == mindbender && ((attacker.type != bomb && attacker.type != crater)
    || (object.has(world::object_flag::special) && !attacker.has(world::object_flag::special)))) return original_partner;
  if(object.type == ground_crew && (object.flags & 0x1000u) != 0) return original_partner;
  if(object.type == aircraft && !aircraft_impact && (geometry.definition(attacker.type).properties & std::to_underlying(world::object_property::runway)) != 0) return original_partner;
  bool const vehicle{object.type == ground_vehicle || object.type == rescue_van};
  if(vehicle) object.position.z = 0;
  auto const react_vehicle{[&]{reset_vehicle(object, current_observer, vehicle_tier(object, current_observer), observer_on_foot, random, hit_vehicle_effects);}};
  if(object.type == ground_crew && (geometry.definition(attacker.type).properties & std::to_underlying(world::object_property::forcefield)) != 0) return original_partner;
  if(object.type == alien && attacker.type != bullets) return original_partner;
  if(object.type == generator_orb && object.quality() == 4 && !aircraft_impact) return original_partner;
  if(vehicle && object.has(world::object_flag::special)
    && attacker.type != bullets && attacker.type != missile && attacker.type != explosion) {
    react_vehicle();
    return original_partner;
  }
  if(object.type == kla) {
    this->explosion(object.position);
    scatter(object, 9, fragment_pattern::outward, tick_step);
    ++statistics.kla_kills[object.quality()];
    object.flags = 0;
    return original_partner;
  }
  unsigned int const hits{aircraft_impact ? 255u : attacker.has(world::object_flag::heavy_attack) ? 25u
    : attacker.has(world::object_flag::six_hit_attack) ? 6u : 1u};
  uint8_t const remaining{static_cast<uint8_t>(object.health - hits)};
  if(!aircraft_impact && remaining != 0 && (remaining & 0x80u) == 0) {
    object.health = remaining;
    if(object.type == kla_site) object.timer = resources::kla_site_qualities[object.quality()].launch_reset;
    if(object.type == saucer_site && !theatre.slalom) object.timer = resources::saucer_site_qualities[object.quality()].launch_reset;
    if(object.type == vehicle_site) object.timer = static_cast<uint16_t>(vehicle_site_rest * 3u);
    if(vehicle) react_vehicle();
    if(object.type == generator_orb && hits >= 6) shatter(object, 2, false);
    if(object.type == tet) {
      // TETTIM leaves DI pointing at a harmless fragment; SLAM consequently skips the original projectile's callback
      return scatter(object, 2, fragment_pattern::outward, tick_step) != 0 ? fragment_partner : original_partner;
    }
    if(damage_handler == saucer || damage_handler == objective_saucer) object.attitude.roll += static_cast<maths::angle>(hits * 400);
    return original_partner;
  }
  uint8_t const old_health{object.health};
  object.health = 0;
  unsigned int const quality{object.quality()};
  bool const friendly_credit{aircraft_impact || (attacker.collision_group == 0 && (attacker.type == bullets || attacker.type == missile
    || (attacker.type == bomb && (attacker.flags & 2u) == 0)))};
  switch(object.type) {
  case generator_orb: ++statistics.orb_kills[quality]; break;
  case kla: ++statistics.kla_kills[quality]; break;
  case ground_vehicle: case rescue_van: ++statistics.vehicle_kills[quality]; break;
  case aaa_site: ++statistics.aaa_kills[quality]; break;
  case kla_site: case mortar: ++statistics.site_kills[0]; break;
  case vehicle_site: case saucer_site: ++statistics.site_kills[quality]; break;
  case fuel_dump: case tet: case saucer: case objective_saucer: case alien: ++statistics.resource_kills; break;
  default: break;
  }
  switch(damage_handler) {
  case generator_orb:
    object.type = destroyed_generator_orb;
    object.flags = static_cast<uint16_t>((object.flags & 0xff00u) | 0x11u);
    scatter(object, 12, fragment_pattern::upward, tick_step);
    shatter(object, 10, false);
    impact_sound();
    break;
  case ground_crew:
    object.flags = static_cast<uint16_t>((object.flags | 0x1000u) & ~8u);
    object.health = old_health;
    if(friendly_credit) ++statistics.friendly_destroyed;
    return original_partner;
  case aircraft:
  case radar_dish:
  case control_tower:
    object.pose = world::model_pose::radar_wreck;
    object.type = destroyed_fuel_dump;
    object.position.z = 3100;
    object.attitude.heading = random.next();
    object.flags = 0x11;
    if(friendly_credit) ++statistics.friendly_destroyed;
    scatter(object, 12, fragment_pattern::upward, tick_step);
    shatter(object, 5, false);
    break;
  case internal_aircraft:
    object.pose = world::model_pose::damaged;
    object.position.z = 0;
    object.velocity = maths::fixed_vector{0, 0, 0};
    object.flags = 1;
    if(friendly_credit) ++statistics.friendly_destroyed;
    scatter(object, 15, fragment_pattern::upward, tick_step);
    shatter(object, 15, false);
    break;
  case fuel_dump:
    object.pose = world::model_pose::fuel_wreck;
    object.type = destroyed_fuel_dump;
    object.flags = 0x11;
    scatter(object, 12, fragment_pattern::upward, tick_step);
    impact_sound();
    break;
  case ground_vehicle:
  case rescue_van:
    {
      int32_t constexpr wreck_height{428};
      object.health = 255;
      this->explosion(object.position);
      scatter(object, 9, fragment_pattern::outward, tick_step);
      object.position.z = wreck_height;
      object.attitude.pitch = 2400;
      object.attitude.roll = 20'000;
      object.flags = static_cast<uint16_t>((object.flags & 0xff00u) | 0x71u);
      object.velocity = maths::fixed_vector{0, 0, 0};
      object.timer = static_cast<uint16_t>(lifetime(800, tick_step) / 4 + 300);
      object.collision_extents = maths::fixed_vector{0, 0, 0};
    }
    break;
  case objective_saucer:
    complete(object);
    [[fallthrough]];
  case saucer:
    object.flags &= static_cast<uint16_t>(~8u);
    this->explosion(object.position);
    scatter(object, 9, fragment_pattern::outward, tick_step);
    object.motion.spin = maths::fixed_vector{static_cast<int16_t>(random.below(4000)), static_cast<int16_t>(random.below(4000)),
      static_cast<int16_t>(random.below(4000))};
    break;
  case alien:
    object.position.z = 0;
    object.attitude.pitch = 16'384;
    object.flags &= static_cast<uint16_t>(~0xcu);
    object.motion.gravity = maths::word(static_cast<uint16_t>(object.motion.gravity) & 0xff00u);
    object.velocity.z = 100;
    break;
  case mindbender:
  case tet:
    this->explosion(object.position, 0x20);
    shatter(object, 3, true);
    object.flags = 0;
    if(object.type == tet) return scatter(object, 2, fragment_pattern::outward, tick_step) != 0 ? fragment_partner : original_partner;
    break;
  case kla_site:
  case vehicle_site:
  case aaa_site:
  case saucer_site:
  case mortar:
    this->explosion(object.position);
    scatter(object, object.type == aaa_site ? 4 : 9, object.type == aaa_site ? fragment_pattern::upward : fragment_pattern::outward, tick_step);
    object.flags = 0;
    break;
  default:
    return original_partner;
  }
  return original_partner;
}

void combat::collide(unsigned int const a, unsigned int const b) {
  /// SLAM calls both type handlers in order, even when the first contact changes the first object's flags
  if(!objects_overlap(theatre.objects[a], theatre.objects[b])) return;
  if(damage(a, theatre.objects[b]) == contact_continuation::original_partner) damage(b, theatre.objects[a]);
}
void combat::update_projectile(unsigned int const index, maths::world_position const &observer, flight::time_step const step) {
  /// Timed handlers alter live velocity and model state before the next generic movement pass
  auto &object{theatre.objects[index]};
  using enum world::object_type;
  switch(object.type) {
  case explosion:
    if(object.timer <= 3) object.pose = object.timer <= 2 ? world::model_pose::red_explosion : world::model_pose::yellow_explosion;
    break;
  case missile:
    if(object.position.z == 0) {
      detonate(index);
      return;
    }
    if(object.timer >= 89 && maths::magnitude(object.velocity) <= 3000) {
      for(unsigned int axis{0}; axis != 3; ++axis) object.velocity[axis] = maths::word(static_cast<int32_t>(object.velocity[axis]) * 1000 / 890);
    }
    break;
  case bomb:
    if((object.flags & 4u) != 0) return;
    if(object.position.z <= 0) {
      detonate(index);
      return;
    }
    {
      auto const delta{vec3<int64_t>{object.position} - vec3<int64_t>{observer}};
      if(delta.length_sq() >= 12'800ll * 12'800) object.flags |= 0xe8u;
      auto const speed{maths::magnitude(object.velocity)};
      for(unsigned int axis{0}; axis != 3; ++axis) object.velocity[axis] = maths::word(object.velocity[axis]
        - static_cast<int32_t>(object.velocity[axis]) * speed / 4630 - (axis == 2 ? 54 : 0));
    }
    break;
  case crater:
    if(object.has(world::object_flag::special)) {
      ++object.timer;
      object.flags |= 0xe0u;
    } else if(object.timer <= crater_arm_timer) object.flags |= 0xe0u;
    break;
  case incendiary_fragment:
    if(static_cast<uint16_t>(object.position.z) == 0) {
      auto const source{object};
      this->explosion(source.position, 0x20);
      scatter(source, 3, fragment_pattern::upward, step);
      object.flags = 0;
    }
    break;
  case flotsam_1:
  case flotsam_2:
    if(object.position.z == 0) object.flags = 0;
    break;
  default:
    break;
  }
  (void)step;
}
void combat::move(maths::world_position const &observer, flight::time_step const step, bool const on_foot, uint32_t const fast_ticks) {
  /// Advance independent effect/bullet lifetimes and retain fixed pool indices for collisions and later saves
  tick_step = step;
  current_observer = maths::world_position{observer};
  observer_on_foot = on_foot;
  current_tick = fast_ticks;
  for(unsigned int i{scenery_count}; i != scenery_count + effect_capacity; ++i) {
    auto &object{theatre.objects[i]};
    if(object.flags == 0) continue;
    object.flags &= 0x7fffu;
    if(object.timer == 0 || (object.timer & 0x8000u) != 0) {
      if((static_cast<uint16_t>(object.motion.gravity) & 0xff00u) != 0) explosion(object.position, 0x20);
      object.flags = 0;
      continue;
    }
    --object.timer;
    if(object.type != world::object_type::bomb || (object.flags & 4u) == 0)
      world::advance_motion(object, geometry.definition(object.type).rotate_below_tier != 0, random, step);
    update_projectile(i, observer, step);
  }
  for(unsigned int i{scenery_count + effect_capacity}; i != theatre.objects.size(); ++i) {
    auto &object{theatre.objects[i]};
    if(object.flags == 0) continue;
    if(object.timer == 0 || (object.timer & 0x8000u) != 0) {
      object.flags = 0;
      continue;
    }
    --object.timer;
    for(unsigned int axis{0}; axis != 3; ++axis) {
      object.velocity[axis] = maths::word(object.velocity[axis] + random.below(40) - 20);
      int16_t const displacement{maths::word(object.velocity[axis] - (axis == 2 ? 54 : 0))};
      object.position[axis] = maths::dword(static_cast<int64_t>(object.position[axis])
        + maths::rounded_divide(static_cast<int32_t>(displacement) * step.elapsed, step.normal));
    }
    if(object.position.z < 0) {
      object.position.z = 0;
      object.velocity = maths::fixed_vector{0, 0, 0};
      object.timer = 0;
      if(auto const puff{spawn(world::object_type::puff, object.position)}) theatre.objects[*puff].timer = 7;
    }
  }
}
void combat::collisions(maths::world_position const &observer) {
  /// Process contacts after timed actor handlers, using stable pool indices
  for(unsigned int a{0}; a != theatre.objects.size(); ++a) {
    (void)observer;
    if(!theatre.objects[a].has(world::object_flag::present)) continue;
    for(unsigned int b{a + 1}; b != theatre.objects.size(); ++b) {
      if(!theatre.objects[b].has(world::object_flag::present)) continue;
      if(theatre.objects[b].type == world::object_type::bullets || theatre.objects[b].type == world::object_type::missile) collide(b, a);
      else collide(a, b);
      if(!theatre.objects[a].has(world::object_flag::present)) break;
    }
  }
}
void combat::advance(maths::world_position const &observer, flight::time_step const step) {
  /// Standalone weapon checks use the same movement/contact stages as the live session
  move(observer, step);
  collisions(observer);
}
void combat::rearm_person() noexcept {
  /// Every ejection supplies the native twenty pistol rounds and one assassin charge
  pistol_rounds = 20;
  charge_available = true;
}
void combat::rearm() noexcept {
  /// Boarding resets the aircraft's three-bomb supply, leaving carried pistol ammunition separate
  bombs = bomb_capacity;
}
sortie_statistics const &combat::stats() const noexcept {
  /// Present shared destruction and objective totals to the campaign/scoring owner
  return statistics;
}
sound_cues const &combat::sounds() const noexcept {
  /// Monotonic event counters let audio consume actual successful launches and blasts without owning simulation state
  return cues;
}
unsigned int combat::bomb_supply() const noexcept {
  /// Report remaining aircraft bombs for cockpit indicators
  return bombs;
}
unsigned int combat::ammunition() const noexcept {
  /// The on-foot pistol has twenty shots rather than the aircraft's replenishing gun counter
  return pistol_rounds;
}

void combat::observe_defences(quality_counts const &aaa, quality_counts const &kla) {
  /// INITNUMBERS records the distribution at each new weighted local-defence maximum
  auto weighted{[](quality_counts const &counts){
    uint16_t sum{0};
    for(unsigned int q{0}; q != counts.size(); ++q) sum = static_cast<uint16_t>(sum + (q + 1) * counts[q]);
    return sum;
  }};
  if(weighted(aaa) > weighted(statistics.peak_aaa)) statistics.peak_aaa = aaa;
  if(weighted(kla) > weighted(statistics.peak_kla)) statistics.peak_kla = kla;
}
unsigned int combat::stored_objects() const noexcept {
  /// Distinguish persistent theatre records from the fixed transient pools
  return scenery_count;
}

} // namespace corncob::game

#include "actors.h"
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cmath>
#include "game/timing.h"
#include "vectorstorm/vector/vector2.h"
#include "maths/integer.h"
#include "resources/actor_tables.h"
#include "world/motion.h"

namespace corncob::game {

namespace {

void count_actor(world::placed_object const &object, actor_census &counts) {
  /// Native census groups count living actors; concealed fuel and rescue vans also contribute to their shared resource/vehicle totals
  using enum world::object_type;
  if(object.type == alien && (object.flags & 4u) == 0) return;
  if((object.type == ground_vehicle || object.type == rescue_van) && (object.flags & 255u) == 0x71u) return;
  ++counts[std::to_underlying(object.type)];
  if(object.type == shell && object.quality() == 6) ++counts[std::to_underlying(fuel_dump)];
  if(object.type == rescue_van) ++counts[std::to_underlying(ground_vehicle)];
}

struct target_vector {
  maths::fixed_vector direction{0, 0, 0};
  uint16_t distance;
  maths::angles bearing;
};
std::optional<target_vector> relative_target(maths::world_position const &object, maths::world_position const &target, unsigned int const shift = 6) {
  /// GETOBSVEC and RADARPROCESS use different word scales but the same eight-high-word component gate
  target_vector result{};
  for(unsigned int axis{0}; axis != 3; ++axis) {
    auto const delta{maths::dword(static_cast<int64_t>(object[axis]) - target[axis])};
    if(std::abs(static_cast<int64_t>(delta)) >= 524'288) return std::nullopt;
    result.direction[axis] = maths::negate(maths::word(delta >> shift));
  }
  maths::fixed_vector const horizontal{result.direction.x, result.direction.y, 0};
  uint16_t const horizontal_distance{maths::magnitude(horizontal)};
  result.distance = shift == 4 ? horizontal_distance : maths::magnitude(result.direction);
  result.bearing = {
    .heading{static_cast<maths::angle>(maths::arctangent(maths::negate(result.direction.x), maths::negate(result.direction.y)) + 32'768)},
    .pitch{maths::arctangent(maths::word(horizontal_distance), maths::negate(result.direction.z))},
  };
  return result;
}
int16_t timed(int16_t const value, flight::time_step const step) noexcept {
  /// WTADJ uses signed truncation for turn limits and small per-tick adjustments
  return maths::word(static_cast<int32_t>(value) * step.elapsed / step.normal);
}
int16_t steer(maths::angle const desired, maths::angle &current, uint16_t const limit, flight::time_step const step) noexcept {
  /// Ground guns and KLAs ease one sixteenth of the wrapped angular error, bounded by quality
  int16_t const error{static_cast<int16_t>(maths::word(desired - current) >> 4)};
  current += static_cast<maths::angle>(timed(static_cast<int16_t>(std::clamp<int>(error, -limit, limit)), step));
  return error;
}
bool objective_in_range(world::placed_object const &object, resources::object_definition const &definition, maths::world_position const &observer) {
  /// HQTIM always checks near-tier anchors and otherwise compares the tier-scaled squared distance with the middle limit
  auto const delta{vec3<int64_t>{object.position} - vec3<int64_t>{observer}};
  int64_t const maximum{std::max({std::abs(delta.x), std::abs(delta.y), std::abs(delta.z)})};
  if(maximum < 32'768) return true;
  unsigned int const shift{maximum < 524'288 ? 4u : 8u};
  int64_t distance{0};
  for(unsigned int axis{0}; axis != 3; ++axis) {
    int64_t const component{delta[axis] >> shift};
    distance += component * component;
  }
  return distance <= definition.limits.middle_squared;
}

} // anonymous namespace

actors::actors(resources::geometry_bank const &geometry, world::theatre &theatre, combat &weapons) :
  geometry{geometry},
  theatre{theatre},
  weapons{weapons},
  random{weapons.random_stream()} {
  /// Keep the preceding active census separate from this pass's live actor updates
  active_updates.reserve(theatre.objects.size());
}
actor_census actors::census(maths::world_position const &centre) const {
  actor_census result{};
  for(auto const &object : theatre.objects) if(object.has(world::object_flag::present) && object.has(world::object_flag::updates)
    && world::in_active_neighbourhood(object.position, centre)) count_actor(object, result);
  return result;
}
void actors::update_launcher(world::placed_object &object, actor_context const &context, flight::time_step const step) {
  /// Site countdowns use the previous census, updating its population immediately when a constructor is attempted
  using enum world::object_type;
  auto const local{census(object.position)};
  auto const population{[&](world::object_type const type){return local[std::to_underlying(type)];}};
  unsigned int const quality{object.quality()};
  world::object_type const launched{object.type == vehicle_site ? ground_vehicle : object.type == saucer_site ? saucer : kla};
  unsigned int const present{population(launched) + (launched == saucer ? population(objective_saucer) : 0)};
  unsigned int const sites{population(object.type)};
  if(object.type == vehicle_site) {
    unsigned int const rest{resources::vehicle_site_qualities[quality].launch_reset};
    weapons.observe_vehicle_site(static_cast<uint16_t>(rest));
    if(object.timer >= rest) return;
    object.timer = static_cast<uint16_t>(rest * 3);
    if(present >= std::max(sites, 4u)) return;
  } else {
    unsigned int constexpr launch_threshold{100};
    if(object.timer >= launch_threshold) return;
    if(present >= sites || (launched == saucer && random.next() > static_cast<uint16_t>(timed(100, step)))) {
      // counteract the ordinary timer decrement while waiting for room or a successful random launch
      ++object.timer;
      return;
    }
    object.timer = launched == kla ? resources::kla_site_qualities[quality].launch_reset : resources::saucer_site_qualities[quality].launch_reset;
  }
  ++previous_counts[std::to_underlying(launched)];
  // the vehicle-site class changes even when its attempted allocation fails
  if(launched == ground_vehicle) object.collision_group = static_cast<uint8_t>(-static_cast<int>(present + 2));
  auto const index{weapons.spawn(launched, object.position)};
  if(!index) return;
  auto &spawned{theatre.objects[*index]};
  spawned.flags = static_cast<uint16_t>((spawned.flags & ~0x7000u) | (quality << 12) | 0x8000u);
  spawned.attitude = {};
  spawned.velocity = maths::fixed_vector{0, 0, 0};
  if(launched == kla) {
    spawned.timer = 24'000;
    spawned.attitude.pitch = static_cast<maths::angle>(-16'384);
  } else if(launched == ground_vehicle) {
    spawned.timer = static_cast<uint16_t>(10'000 * step.normal / step.elapsed);
    spawned.collision_group = object.collision_group;
  } else spawned.timer = 32'000;
  (void)context;
}
void actors::update_kla(world::placed_object &object, actor_context const &context, flight::time_step const step) {
  /// KLAs launch vertically, wait through their hundred-frame rest and then steer rather than teleporting their velocity
  if(object.timer >= 23'900) return;
  object.motion.gravity = static_cast<int16_t>(static_cast<uint16_t>(object.motion.gravity) & 0xff00u);
  object.motion.dispersion = 0;
  object.motion.drag_divisor = 0;
  auto const target{relative_target(object.position, context.target, 4)};
  if(!target) {
    if(object.timer <= 11'950) object.timer = 1;
    return;
  }
  uint16_t const distance{static_cast<uint16_t>(target->distance >> 6)};
  if(distance <= effects.sound.kla_distance) {
    effects.sound.kla_distance = distance;
    last_kla_quality = effects.sound.kla_quality = static_cast<uint8_t>(object.quality());
  }
  auto const &parameters{resources::kla_qualities[object.quality()]};
  int16_t const pitch_error{static_cast<int16_t>(maths::word(target->bearing.pitch-object.attitude.pitch) >> 4)};
  int16_t const heading_error{static_cast<int16_t>(maths::word(target->bearing.heading-object.attitude.heading) >> 4)};
  if(step.seconds == 0) {
    steer(target->bearing.pitch, object.attitude.pitch, parameters.pitch_turn_limit, step);
    steer(target->bearing.heading, object.attitude.heading, parameters.heading_turn_limit, step);
    object.attitude.roll += static_cast<maths::angle>(timed(maths::word(parameters.roll_rate), step));
  }
  unsigned int const error{static_cast<unsigned int>(std::max(std::abs(static_cast<int>(pitch_error)), std::abs(static_cast<int>(heading_error))))};
  int target_speed{parameters.normal_target_speed};
  if(error <= parameters.lock_angle_limit && target->distance >= 200) target_speed = parameters.locked_target_speed;
  else if(error > parameters.lock_angle_limit) target_speed = parameters.off_axis_target_speed;
  int speed{maths::magnitude(object.velocity)};
  speed += (target_speed - speed) / parameters.speed_relaxation_divisor
    + random.below(parameters.speed_noise_span) - parameters.speed_noise_span / 2;
  object.velocity = maths::transform(maths::make_inverse_rotation(object.attitude), {maths::word(speed), 0, 0});
  if(object.position.z >= 0 && object.position.z <= 1000 && object.velocity.z < 0) object.velocity.z = 0;
}
void actors::update_aaa(world::placed_object &object, actor_context const &context, flight::time_step const step) {
  /// AAA quality controls tracking, target lead, firing probability and in-flight shell jitter
  auto target{relative_target(object.position, context.target)};
  if(!target) {
    object.velocity = maths::fixed_vector{0, 0, 0};
    return;
  }
  auto const &parameters{resources::aaa_qualities[object.quality()]};
  int16_t constexpr shell_speed{1500};
  if(parameters.lead_target != 0) {
    for(unsigned int axis{0}; axis != 3; ++axis) target->direction[axis] = maths::word(target->direction[axis]
      + static_cast<int32_t>(maths::word(target->distance)) * (context.target_velocity[axis] >> 4) / shell_speed);
    maths::fixed_vector const horizontal{target->direction.x, target->direction.y, 0};
    target->bearing.heading = static_cast<maths::angle>(maths::arctangent(maths::negate(target->direction.x), maths::negate(target->direction.y)) + 32'768);
    target->bearing.pitch = maths::arctangent(maths::word(maths::magnitude(horizontal)), maths::negate(target->direction.z));
  }
  if(step.seconds == 0) {
    steer(target->bearing.pitch, object.attitude.pitch, parameters.pitch_turn_limit, step);
    steer(target->bearing.heading, object.attitude.heading, parameters.heading_turn_limit, step);
  }
  if(object.position.z >= 0 && object.position.z <= 2000 && maths::word(object.attitude.pitch) >= -1200)
    object.attitude.pitch = static_cast<maths::angle>(-1200);
  if(random.next() >= parameters.fire_threshold) return;
  auto const index{weapons.spawn(world::object_type::aa_shell, object.position)};
  if(!index) return;
  auto &shell{theatre.objects[*index]};
  shell.position.z += 1600;
  shell.velocity = maths::transform(maths::make_inverse_rotation(object.attitude), {shell_speed, 0, 0});
  shell.timer = static_cast<uint16_t>(150 * step.normal / step.elapsed);
  shell.motion.dispersion = maths::word(parameters.shell_jitter);
}
void actors::update_saucer(world::placed_object &object, actor_context const &context, flight::time_step const step) {
  /// Saucers cycle attack, retreat, sideways and coasting modes around the current airbase rather than chasing the pilot directly
  if(auto const target{relative_target(object.position, context.target, 4)}; target && object.has(world::object_flag::collision_callback)) {
    auto const distance{maths::square_root(target->distance)};
    if(distance <= effects.sound.saucer_distance) {
      effects.sound.saucer_distance = distance;
      // BMRTIM derives pitch from remaining health before any objective-saucer repair
      effects.sound.saucer_frequency = static_cast<uint8_t>(static_cast<unsigned int>(object.health) * 256 / 51 + 2);
    }
  }
  auto const &parameters{resources::saucer_qualities[object.quality()]};
  if(!object.has(world::object_flag::collision_callback)) {
    object.motion.gravity = 15;
    object.motion.dispersion = 20;
    object.motion.drag_divisor = 18'000;
    if(object.position.z < 1000) {
      weapons.scatter(object, 12, fragment_pattern::upward, step);
      weapons.scatter(object, 15, fragment_pattern::incendiary, step);
      object.flags = 0;
      weapons.impact_sound();
    }
    return;
  }
  if(object.type == world::object_type::objective_saucer && object.timer >= 32'700) {
    if((object.flags & 0x4000u) == 0) object.timer = 32'000;
    else if(object.health >= 25) {
      object.timer = 32'767;
      return;
    } else {
      object.health = 50;
      object.timer = 32'000;
    }
  }
  object.timer = static_cast<uint16_t>(std::max(0, static_cast<int>(object.timer) + 1 - timed(3, step)));
  if(step.seconds == 0) {
    object.attitude.roll = static_cast<maths::angle>(object.attitude.roll - (maths::word(object.attitude.roll) >> 4));
    object.attitude.heading += 4000;
  }
  int16_t const height_error{maths::word((object.position.z - 3 * 65'536) >> 8)};
  int16_t rise{maths::word(-height_error / 100)};
  if(std::abs(static_cast<int>(rise)) >= 40) rise = std::clamp<int16_t>(rise, -15, 15);
  object.velocity.z = maths::word(object.velocity.z + timed(rise, step));
  int const damping{std::max(1, 20 * step.normal / step.elapsed)};
  object.velocity.z = maths::word(object.velocity.z - object.velocity.z / damping);
  vec2<int32_t> direction{theatre.start.x - object.position.x, theatre.start.y - object.position.y};
  unsigned int shifts{0};
  while((std::abs(static_cast<int64_t>(direction.x)) > 10 || std::abs(static_cast<int64_t>(direction.y)) > 10) && shifts != 31) {
    direction.x >>= 1;
    direction.y >>= 1;
    ++shifts;
  }
  uint16_t transition{3};
  switch(object.flags & 6u) {
  case 0:
    object.velocity.x = maths::word(object.velocity.x - direction.x);
    object.velocity.y = maths::word(object.velocity.y - direction.y);
    transition = parameters.retreat_exit_threshold;
    break;
  case 2:
    object.velocity.y = maths::word(object.velocity.y + direction.x);
    object.velocity.x = maths::word(object.velocity.x - direction.y);
    transition = parameters.sideways_exit_threshold;
    break;
  case 4:
    object.velocity.x = maths::word(object.velocity.x + direction.x * 2);
    object.velocity.y = maths::word(object.velocity.y + direction.y * 2);
    transition = shifts <= 13 ? 50 : object.health <= 25 ? 20 : 3;
    break;
  case 6:
    transition = parameters.coast_exit_threshold;
    break;
  }
  if((random.next() >> 1) <= static_cast<uint16_t>(timed(maths::word(transition), step)))
    object.flags = static_cast<uint16_t>((object.flags & ~6u) | ((object.flags + 2u) & 6u));
  if(shifts <= 13) {
    if(object.position.z >= 65'536 && (random.next() >> 1) <= parameters.bomb_threshold) {
      if(auto const index{weapons.spawn(world::object_type::bomb, object.position)}) {
        auto &bomb{theatre.objects[*index]};
        bomb.position.z -= 500;
        bomb.velocity = maths::fixed_vector{object.velocity};
        bomb.flags = static_cast<uint16_t>((bomb.flags | 2u) & ~0xe8u);
        bomb.timer = 3200;
      }
    }
  } else if((random.next() >> 1) <= parameters.kla_threshold
    && previous_counts[std::to_underlying(world::object_type::kla)] <= (parameters.weapon_bits & 255u)) {
    if(auto const index{weapons.spawn(world::object_type::kla, object.position)}) {
      auto &kla{theatre.objects[*index]};
      kla.flags = static_cast<uint16_t>((kla.flags & ~0x7000u) | (object.flags & 0x7000u));
      kla.motion.gravity = 20;
      kla.motion.dispersion = 20;
      kla.motion.drag_divisor = 16'000;
      kla.timer = static_cast<uint16_t>(23'900 + 60 * step.normal / step.elapsed);
    }
  }
  unsigned int phase{static_cast<unsigned int>(32'000 - object.timer)};
  if(phase >= 1000) {
    object.timer = 32'000;
    phase = 0;
  }
  phase = phase * static_cast<unsigned int>(step.elapsed) / static_cast<unsigned int>(step.normal);
  if(parameters.vertical_wave_amplitude != 0 && object.position.z >= 65'536) {
    auto const wave{maths::negate(maths::sine(static_cast<maths::angle>(phase * parameters.vertical_wave_frequency * 8)))};
    object.velocity.z = maths::word(object.velocity.z + maths::multiply(wave, maths::word(parameters.vertical_wave_amplitude)));
  }
  if(parameters.horizontal_wave_amplitude != 0) {
    auto const angle{static_cast<maths::angle>(phase * parameters.horizontal_wave_frequency * 8)};
    object.velocity.x = maths::word(object.velocity.x + maths::multiply(maths::negate(maths::sine(angle)), maths::word(parameters.horizontal_wave_amplitude)));
    object.velocity.y = maths::word(object.velocity.y + maths::multiply(maths::negate(maths::cosine(angle)), 100));
  }
  int const speed{maths::magnitude(object.velocity)};
  int const desired{speed + (static_cast<int>(parameters.target_speed) - speed) / parameters.horizontal_wave_frequency
    + random.below(parameters.speed_noise_span) - parameters.speed_noise_span / 2};
  // the original only reduces this magnitude; acceleration comes from its mode and wave terms
  if(desired >= 0 && desired <= speed && speed != 0) {
    for(unsigned int axis{0}; axis != 3; ++axis) object.velocity[axis] = maths::word(static_cast<int32_t>(object.velocity[axis]) * desired / speed);
  }
  if((parameters.weapon_bits >> 8) != 0) {
    world::placed_object gun{
      .position{maths::world_position{object.position}},
      .attitude{},
      .type{world::object_type::aaa_site},
      .flags{static_cast<uint16_t>((object.flags & ~0x7000u) | (std::min(object.quality(), 5u) << 12))},
    };
    update_aaa(gun, context, step);
  }
}
void actors::update_shell(unsigned int const index, actor_context const &context, flight::time_step const step) {
  /// AAA shells burst on their last frame or probabilistically near aircraft altitude, expanding their contact volume
  auto &object{theatre.objects[index]};
  if(object.timer == 1) {
    weapons.detonate_object(index);
    return;
  }
  if(object.position.z >= 0 && object.position.z <= 65'535 && (static_cast<uint32_t>(object.position.z) >> 1) <= static_cast<uint16_t>(object.collision_extents.x)) return;
  auto const delta{maths::dword(static_cast<int64_t>(object.position.z) - context.target.z)};
  if(std::abs(static_cast<int64_t>(delta)) <= 4500 && random.next() <= static_cast<uint16_t>(timed(3000, step))) {
    weapons.detonate_object(index);
    return;
  }
  object.collision_extents = maths::fixed_vector{6000, 6000, 6000};
}
void actors::update_vehicle(world::placed_object &object, actor_context const &context, flight::time_step const step) {
  /// VECHTIM distinguishes the renderer's distance tiers from actual capture distance, and preserves RESETVECH's random/follow ordering
  if((object.flags & 255u) == 0x71u) {
    if(object.timer == 0) object.flags = 0;
    return;
  }
  auto const tier{vehicle_tier(object, context.observer)};
  object.position.z = 0;
  auto const &parameters{resources::vehicle_qualities[object.quality()]};
  if(object.has(world::object_flag::special)) ++object.timer;
  uint16_t threshold{static_cast<uint16_t>(timed(maths::word(parameters.change_direction_threshold), step))};
  bool const stopped_nearby{tier == 0 && (object.flags & 4u) != 0};
  if(tier == 0 && !stopped_nearby) threshold = static_cast<uint16_t>(threshold * 4);
  if(stopped_nearby || random.next() <= threshold) reset_vehicle(object, context.observer, tier, context.on_foot, random, effects);
  if((object.flags & 4u) != 0) {
    object.velocity = maths::fixed_vector{0, 0, 0};
    if(object.collision_group != 1) follow_vehicle(object, tier);
    return;
  }
  auto const phase{static_cast<maths::angle>(context.timer * parameters.weave_frequency)};
  object.attitude.heading += static_cast<maths::angle>(maths::sine(phase) / parameters.weave_divisor);
  follow_vehicle(object, tier);
}

void actors::update_alien(world::placed_object &object, actor_context const &context, flight::time_step const /*step*/) {
  /// Aliens activate around the on-foot pilot and hop with quality-specific gravity and horizontal drift
  ++object.timer;
  if((object.flags & 4u) == 0) {
    auto const delta{vec3<int64_t>{object.position} - vec3<int64_t>{context.observer}};
    if(std::max({std::abs(delta.x), std::abs(delta.y), std::abs(delta.z)}) >= 32'768) object.flags = 0;
    return;
  }
  if(context.on_foot) {
    auto const delta{vec3<int64_t>{object.position} - vec3<int64_t>{context.observer}};
    if(std::max({std::abs(delta.x), std::abs(delta.y), std::abs(delta.z)}) < 32'768) {
      object.timer = 16'000;
      object.flags |= 8u;
    }
  }
  if((object.timer & 16'384u) != 0 || !object.has(world::object_flag::collision_callback) || object.position.z > 0) return;
  auto const &parameters{resources::alien_qualities[object.quality()]};
  object.position.z = 0;
  object.velocity.z = maths::word(parameters.jump_velocity);
  object.motion.gravity = maths::word(parameters.gravity);
  auto hop{[&](int16_t const previous, uint16_t const span){
    int16_t const value{maths::word(previous + random.below(span) - span / 2)};
    return maths::word(value - (value >> 3));
  }};
  object.velocity.x = hop(object.velocity.x, parameters.horizontal_jitter);
  object.velocity.y = hop(object.velocity.y, 50);
  object.attitude.heading = static_cast<maths::angle>(hop(maths::word(object.attitude.heading), 1000));
}
void actors::update_balloon(world::placed_object &object, flight::time_step const step) {
  /// Barrage balloons bounce vertically and occasionally reverse while bees also randomise all three angles
  bool skip_movement{false};
  if(object.type == world::object_type::bee_swarm) {
    object.attitude.heading = random.next();
    object.attitude.pitch = random.next();
    auto const roll{random.next_with_carry()};
    object.attitude.roll = roll.value;
    skip_movement = roll.carry;
  }
  object.timer = 32'000;
  if(skip_movement) return;
  auto const &parameters{resources::balloon_qualities[object.quality()]};
  int16_t const speed{timed(maths::word(parameters.vertical_launch_speed), step)};
  if(object.velocity.z == 0 || object.position.z < 16'383) object.velocity.z = speed;
  else if(object.position.z > 65'535) object.velocity.z = maths::negate(speed);
  else if(random.next() <= static_cast<uint16_t>(timed(maths::word(parameters.reversal_threshold), step))) object.velocity.z = maths::negate(object.velocity.z);

}
void actors::update_orb(world::placed_object &object, actor_context const &context, flight::time_step const step) {
  /// Orb objectives inspect their own installation; destruction leaves the objective anchor in place.
  object.timer = 32'000;
  if(object.type == world::object_type::destroyed_generator_orb && object.has(world::object_flag::objective_complete)) {
    unsigned int constexpr collapse_height{11'200}, resting_height{3100};
    if((static_cast<uint32_t>(object.position.z) >> 16) != 0 || static_cast<uint16_t>(object.position.z) > collapse_height) {
      object.motion.spin = maths::fixed_vector{static_cast<int16_t>(random.below(60) + 20), static_cast<int16_t>(random.below(40) + 10), static_cast<int16_t>(random.below(110) + 30)};
      object.motion.gravity = maths::word((static_cast<uint16_t>(object.motion.gravity) & 0xff00u) | 35u);
    } else {
      object.position.z = resting_height;
      object.velocity = maths::fixed_vector{0, 0, 0};
      object.motion.dispersion = object.motion.drag_divisor = 0;
    }
  }
  if(object.type == world::object_type::generator_orb) {
    auto const target{relative_target(object.position, context.observer)};
    if(target && target->distance <= 2000) {
      effects.sound.orb_distance = std::min(effects.sound.orb_distance, static_cast<uint16_t>(std::min(63, target->distance * 70 / 2000)));
      effects.orb_distortion = std::max(effects.orb_distortion, static_cast<uint16_t>(std::min(35, 71 - target->distance * 70 / 2000)));
      if(object.has(world::object_flag::special) && !context.on_foot) {
        unsigned int const divisor{std::max(10u, static_cast<unsigned int>(target->distance) * target->distance / 5000u)};
        for(unsigned int axis{0}; axis != 3; ++axis) {
          int32_t const delta{maths::dword(static_cast<int64_t>(object.position[axis]) - context.observer[axis])};
          auto const acceleration{maths::word((delta >> 3) / static_cast<int>(divisor))};
          effects.attraction[axis] = maths::word(effects.attraction[axis] + timed(acceleration, step));
        }
      }
    }
  }
  if(!objective_in_range(object, geometry.definition(object.type), context.observer)) return;
  if(object.quality() == 4) object.health = 65;
  auto const local{census(object.position)};
  auto population{[&](world::object_type const type){return local[std::to_underlying(type)];}};
  using enum world::object_type;
  bool complete{false};
  switch(object.quality()) {
  case 0:
  case 4: complete = object.type == destroyed_generator_orb; break;
  case 1: complete = population(fuel_dump) + population(tet) == 0; break;
  case 2: complete = population(kla_site) + population(aaa_site) == 0; break;
  case 3: complete = population(ground_vehicle) + population(vehicle_site) == 0; break;
  case 5: complete = population(saucer) + population(objective_saucer) + population(saucer_site) == 0; break;
  case 6: complete = population(mindbender) == 0; break;
  case 7: complete = observed_aliens != 0 && population(alien) == 0; break;
  }
  if(complete) weapons.complete(object);
}
void actors::update_mortar(world::placed_object &object, actor_context const &context, flight::time_step const step) {
  /// MORTIM waits six frames between upward two-piece barrages and only fires in its near distance tier
  unsigned int constexpr full_timer{10'000}, ready_timer{full_timer - 6};
  if(object.timer > ready_timer) return;
  object.timer = ready_timer;
  auto const delta{vec3<int64_t>{object.position} - vec3<int64_t>{context.observer}};
  if(std::max({std::abs(delta.x), std::abs(delta.y), std::abs(delta.z)}) >= 32'768) return;
  if(std::abs(delta.x) + std::abs(delta.y) > 130'000'000) return;
  object.timer = full_timer;
  weapons.scatter(object, 2, fragment_pattern::mortar, step);
}
proximity_sound const &actors::proximity() const noexcept {
  /// Retain the nearest active proximity sources, independently of actor order and distant updates.
  return effects.sound;
}

void actors::refresh_census(maths::world_position const &observer) {
  /// Prime freshly loaded/relocated neighbourhoods before goals consume the preceding active-object counts
  previous_counts.fill(0);
  for(auto const &object : theatre.objects) {
    if(object.has(world::object_flag::present) && object.has(world::object_flag::updates)
      && world::in_active_neighbourhood(object.position, observer)) {
      count_actor(object, previous_counts);
    }
  }
  observed_aliens = std::max(observed_aliens, previous_counts[std::to_underlying(world::object_type::alien)]);
  census_ready = true;
}
void actors::steering_elapsed(actor_context const &context, double const seconds) {
  double const ratio{seconds/timing::seconds_per_step};
  auto turn{[&](maths::angle desired, maths::angle &current, uint16_t limit, double &fraction){
    double const error{maths::word(desired-current)-fraction};
    double const change{std::clamp(error*(1-maths::decay(15.0/16.0,ratio)), -limit*ratio, limit*ratio)};
    maths::turn(current, change, fraction);
  }};
  for(auto &object : theatre.objects) {
    if(!object.has(world::object_flag::present) || !object.has(world::object_flag::updates)) continue;
    if(object.type == world::object_type::kla && object.timer < 23900) {
      auto const target{relative_target(object.position,context.target,4)};
      if(!target) continue;
      auto const &parameters{resources::kla_qualities[object.quality()]};
      turn(target->bearing.pitch, object.attitude.pitch, parameters.pitch_turn_limit, object.fraction.angles.y);
      turn(target->bearing.heading, object.attitude.heading, parameters.heading_turn_limit, object.fraction.angles.x);
      maths::turn(object.attitude.roll, maths::word(parameters.roll_rate)*ratio, object.fraction.angles.z);
      double const speed{(vec3<double>{object.velocity}+object.fraction.velocity).length()};
      auto const velocity{maths::rotation_real(maths::real_angles(object.attitude,object.fraction.angles)).apply({speed,0,0})};
      for(unsigned int axis{0}; axis < 3; ++axis) {
        object.velocity[axis] = maths::word(static_cast<int32_t>(std::trunc(velocity[axis])));
        object.fraction.velocity[axis] = velocity[axis]-std::trunc(velocity[axis]);
      }
      if(object.position.z >= 0 && object.position.z <= 1000 && object.velocity.z < 0) { object.velocity.z = 0; object.fraction.velocity.z = 0; }
    } else if(object.type == world::object_type::aaa_site) {
      auto target{relative_target(object.position,context.target)};
      if(!target) continue;
      auto const &parameters{resources::aaa_qualities[object.quality()]};
      if(parameters.lead_target != 0) {
        for(unsigned int axis{0}; axis < 3; ++axis) target->direction[axis] = maths::word(target->direction[axis]
          + static_cast<int32_t>(maths::word(target->distance))*(context.target_velocity[axis] >> 4)/1500);
        maths::fixed_vector const horizontal{target->direction.x,target->direction.y,0};
        target->bearing.heading = static_cast<maths::angle>(maths::arctangent(maths::negate(target->direction.x),maths::negate(target->direction.y))+32768);
        target->bearing.pitch = maths::arctangent(maths::word(maths::magnitude(horizontal)),maths::negate(target->direction.z));
      }
      turn(target->bearing.pitch,object.attitude.pitch,parameters.pitch_turn_limit,object.fraction.angles.y);
      turn(target->bearing.heading,object.attitude.heading,parameters.heading_turn_limit,object.fraction.angles.x);
    } else if((object.type == world::object_type::saucer || object.type == world::object_type::objective_saucer)
      && object.has(world::object_flag::collision_callback)) {
      maths::turn(object.attitude.heading,4000*ratio,object.fraction.angles.x);
      maths::turn(object.attitude.roll,-maths::word(object.attitude.roll)*(1-maths::decay(15.0/16.0,ratio)),object.fraction.angles.z);
    }
  }
}

actor_effects const &actors::advance(actor_context const &context, flight::time_step const step) {
  /// Update actors throughout the world; local objectives and production use installation-centred censuses.
  if(step.seconds > 0) steering_elapsed(context, step.seconds);
  if(step.seconds > 0 && !step.clock_tick) {
    for(unsigned int i{0}; i < weapons.stored_objects(); ++i) {
      auto &object{theatre.objects[i]};
      if(object.has(world::object_flag::present) && object.has(world::object_flag::updates))
        world::advance_motion(object, geometry.definition(object.type).rotate_below_tier != 0, random, step);
    }
    return effects;
  }
  effects = {};
  static_cast<vehicle_effects &>(effects) = weapons.take_vehicle_effects();
  effects.sound.kla_quality = last_kla_quality;
  if(!census_ready) refresh_census(context.observer);
  flight::time_step const decisions{step.elapsed, step.normal, step.seconds > 0 ? timing::seconds_per_step : 0};
  quality_counts aaa{}, kla{};
  actor_census counts{};
  active_updates.clear();
  for(unsigned int i{0}; i != theatre.objects.size(); ++i) {
    auto const &object{theatre.objects[i]};
    if(object.has(world::object_flag::present) && object.has(world::object_flag::updates)
      ) active_updates.push_back(i);
  }
  // new effects enter the original painter tables at the following EXPMOV, after their creation pass
  for(unsigned int const i : active_updates) {
    auto &object{theatre.objects[i]};
    if(!object.has(world::object_flag::present)) continue;
    auto const &definition{geometry.definition(object.type)};
    if(!object.has(world::object_flag::updates)) continue;
    if(i < weapons.stored_objects()) {
      if(object.timer != 0) --object.timer;
      if(object.motion.spin != maths::fixed_vector{0, 0, 0} || object.motion.gravity != 0 || object.motion.dispersion != 0 || object.velocity != maths::fixed_vector{0, 0, 0})
        world::advance_motion(object, definition.rotate_below_tier != 0, random, step);
    }
    count_actor(object, counts);
    if(object.type == world::object_type::aaa_site) ++aaa[object.quality()];
    if(object.type == world::object_type::kla) ++kla[object.quality()];
    using enum world::object_type;
    if(theatre.slalom && (object.type == shell || object.type == saucer || object.type == objective_saucer || object.type == mortar || object.type == saucer_site)) {
      // Slalom's farm/tree/radar-marker handlers omit combat spawning and shell transformations.
      ++object.timer;
      if(object.type == saucer_site) {
        int16_t speed{150};
        if((object.flags & 2u) != 0) speed = (object.flags & 4u) != 0 ? 0 : 37;
        object.velocity = maths::transform(maths::make_inverse_rotation(object.attitude), maths::fixed_vector{speed, 0, 0});
      }
      continue;
    }
    switch(object.type) {
    case kla_site:
    case vehicle_site:
    case saucer_site: update_launcher(object, context, decisions); break;
    case kla: update_kla(object, context, decisions); break;
    case aaa_site: update_aaa(object, context, decisions); break;
    case mortar: update_mortar(object, context, decisions); break;
    case shell:
      if((object.timer & 16'384u) != 0) {
        if(32'000 - object.timer > 30 * step.normal / step.elapsed) object.timer = 16'000;
      } else if(16'000 - object.timer > std::max(2, 5 * step.normal / step.elapsed)) object.timer = 32'000;
      break;
    case aa_shell: update_shell(i, context, decisions); break;
    case shard:
    case shard_2:
      object.shard_colour = static_cast<uint16_t>(object.position.z) != 0 && ((i - weapons.stored_objects()) & 3u) == 0
        ? static_cast<uint8_t>(random.below(2) * 2 + 12) : 0;
      break;
    case saucer:
    case objective_saucer: update_saucer(object, context, decisions); break;
    case ground_vehicle:
    case rescue_van: update_vehicle(object, context, decisions); break;
    case alien: update_alien(object, context, decisions); break;
    case barrage_balloon:
    case bee_swarm: update_balloon(object, decisions); break;
    case generator_orb:
    case destroyed_generator_orb: update_orb(object, context, decisions); break;
    case mindbender:
      ++object.timer;
      if(auto const target{relative_target(object.position, context.observer)}; target && target->distance <= 1000) {
        object.flags |= 4u;
        int const proximity{1000 - target->distance};
        effects.hallucination = std::max(effects.hallucination, static_cast<uint16_t>(proximity * proximity / 31));
      }
      break;
    case portal:
      {
        auto const &parameters{resources::portal_qualities[object.quality()]};
        uint8_t colour{static_cast<uint8_t>(object.has(world::object_flag::objective_complete) ? 15 : parameters.colour)};
        if(maths::word(random.next()) >= 0 || (object.timer & 16'384u) == 0) colour = static_cast<uint8_t>(parameters.flash_colour);
        object.portal_colour = colour;
        object.timer = 32'000;
      }
      break;
    case control_tower:
      if(!world::in_active_neighbourhood(object.position, context.observer)) { ++object.timer; break; }
      if(auto const reset{tower_parking_reset(object.position, context.observer)}; reset != parking_reset::unchanged) effects.parking = reset;
      theatre.start.x = maths::dword(static_cast<int64_t>(object.position.x) + 3001 - 2 * 65'536);
      theatre.start.y = maths::dword(static_cast<int64_t>(object.position.y) - 16'000);
      ++object.timer;
      break;
    case fuel_dump:
    case tet:
      ++object.timer;
      break;
    default:
      break;
    }
    if(!theatre.slalom && (definition.properties & std::to_underlying(world::object_property::forcefield)) != 0) {
      auto const local{census(object.position)};
      if(local[std::to_underlying(generator_orb)] == 0 && local[std::to_underlying(destroyed_generator_orb)] != 0) object.flags = 0;
      else if(object.type == forcefield_x || object.type == forcefield_y || object.type == forcefield_z || object.type == forcefield_z1) {
        // quality zero only reveals its far model for eight frames after contact; the nearby barrier always remains visible
        if(object.quality() == 0 && object.timer <= 31'992) {
          object.timer = 31'990;
          object.pose = world::model_pose::distant_hidden;
        }
      } else object.timer = 32'000;
    }
  }
  observed_aliens = std::max(observed_aliens, counts[std::to_underlying(world::object_type::alien)]);
  previous_counts = counts;
  weapons.observe_defences(aaa, kla);
  return effects;
}

} // namespace corncob::game

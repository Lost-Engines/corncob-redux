#include "session.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <utility>
#include "game/frame_clock.h"
#include "game/scenery.h"
#include "game/timing.h"
#include "maths/integer.h"
#include "resources/actor_tables.h"
#include "world/airfields.h"

namespace corncob::game {

namespace {

bool has(resources::object_definition const &definition, world::object_property const property) noexcept {
  /// Keep original type properties distinct from each placed object's changing live flags
  return (definition.properties & std::to_underlying(property)) != 0;
}
int16_t timed_word(int16_t const value, flight::time_step const step) noexcept {
  /// WTADJ truncates a signed product/division, unlike the force routine's rounded dword helper
  return maths::word(static_cast<int32_t>(value) * step.elapsed / step.normal);
}

} // anonymous namespace

session::session(resources::geometry_bank const &geometry, world::theatre theatre, bool const reserve_aircraft) :
  geometry{geometry},
  theatre{std::move(theatre)},
  weapons{geometry, this->theatre},
  enemies{geometry, this->theatre, weapons},
  random{weapons.random_stream()},
  particles{geometry.initial_particles()} {
  if(this->theatre.slalom) course.emplace(this->theatre.start.x);
  /// READUNIV starts the pilot on foot three thousand centimetres south of the stored start
  atmosphere = {
    .gravity{this->theatre.environment.gravity},
    .sea_level_density{this->theatre.environment.sea_level_density},
    .height{this->theatre.environment.atmosphere_height},
    .power_transition{static_cast<uint16_t>(this->theatre.environment.sea_level_density * 2 / 3)},
  };
  person.position = maths::world_position{this->theatre.start};
  person.position.y -= 3000;
  person.attitude = {
    .heading{static_cast<maths::angle>(-8000)},
    .pitch{static_cast<maths::angle>(-1500)},
    .roll{0},
  };
  for(auto &object : this->theatre.objects) {
    auto const &definition{geometry.definition(object.type)};
    object.health = definition.hit_points;
    object.collision_group = definition.collision_group;
    object.motion = definition.motion;
  }
  notice = "On foot - WASD and mouse; Space jumps; F4 shows controls";
  if(this->theatre.training || reserve_aircraft) {
    parking.new_arrival = false;
    if(auto const index{weapons.spawn(world::object_type::aircraft, this->theatre.start)}) {
      if(reserve_aircraft) this->theatre.objects[*index].flags |= 0x1000;
      enter_aircraft(*index);
      if(this->theatre.training) {
        aircraft.position.y -= 8000;
        aircraft.attitude = {};
      }
      aircraft.world_to_body = maths::make_rotation(aircraft.attitude);
      aircraft.body_to_world = maths::make_inverse_rotation(aircraft.attitude);
      retain_aircraft();
    }
  }
  if(course) notice = "Slalom: +/- throttle; six markers, alternating sides; weapons disabled";
}

void session::press(action const command) {
  /// Handle discrete game commands independently of window callbacks for scripted and native checks
  if(command == action::toggle_invulnerability) {
    set_invincible(theatre.expansion ? !invincible : true);
    if(invincible) {
      damage = {};
      damage.invincible = true;
      angular.wing_damage = angular.rudder_damage = angular.stability_damage = 0;
      damage.maximum_throttle = 200;
    }
    return;
  }
  if(command == action::toggle_messages) {
    messages = !messages;
    if(!messages) radio.reset();
    return;
  }
  if(command == action::toggle_map) {
    if(map) map.reset();
    else if(theatre.training) notice = "Map mode is not available in training missions";
    else map.emplace(camera().position);
    return;
  }
  if(map) return;
  if(command == action::pause) {
    if(!paused) clock.suspend();
    paused = !paused;
    return;
  }
  if(command == action::toggle_rear) {
    rear = !rear;
    return;
  }
  if(command == action::toggle_look) {
    look_mode = !look_mode;
    return;
  }
  if(command == action::toggle_ground) {
    ground_detail = !ground_detail;
    return;
  }
  if(command == action::toggle_cockpit) {
    cockpit = !cockpit;
    return;
  }
  if(paused || visiting_tower || finished()) return;
  bool const flying{mode == player_mode::in_aircraft || (remote && controlled_aircraft)};
  switch(command) {
  case action::throttle_up:
    if(!flying || landing.crash_landing) break;
    if(aircraft.variant == flight::aircraft_variant::experimental && !experimental_clearance) {
      notice = "Experimental aircraft clearance required";
      break;
    }
    aircraft.throttle = static_cast<uint16_t>(std::min<unsigned int>(aircraft.throttle + 20, damage.maximum_throttle));
    landing.stopped = false;
    notice = "Engine running - arrows fly, Z/X rudder, E ejects";
    break;
  case action::throttle_down:
    if(flying) aircraft.throttle = static_cast<uint16_t>(std::max(20, static_cast<int>(aircraft.throttle) - 20));
    break;
  case action::toggle_autopilot:
    if(flying) {
      angular.autopilot.engaged = !angular.autopilot.engaged;
      notice = angular.autopilot.engaged ? "Autopilot engaged" : "Autopilot off";
    }
    break;
  case action::toggle_flaps:
    if(flying) aircraft.flaps = !aircraft.flaps;
    break;
  case action::rocket:
    if(flying) rocket_requested = true;
    break;
  case action::eject:
    if(mode == player_mode::in_aircraft) leave_aircraft();
    break;
  case action::parachute:
    if(mode == player_mode::on_foot && !person.standing && !person.parachute) {
      person.parachute = true;
      person.swing_frames = 508;
    }
    break;
  case action::drop_bomb:
    if(!course && flying) weapons.drop_bomb(aircraft, fast_ticks, {});
    break;
  case action::plant_charge:
    if(!course && mode == player_mode::on_foot) weapons.plant_charge(person.position);
    break;
  case action::arm_charges:
    if(!course) weapons.arm_charges();
    break;
  case action::call_rescue:
    if(mode == player_mode::on_foot && person.standing && !at_home()
      && std::ranges::none_of(theatre.objects, [](auto const &object){return object.type == world::object_type::rescue_van && object.has(world::object_flag::present);})) {
      auto position{maths::world_position{theatre.start}};
      for(unsigned int attempt{0}; attempt != 16; ++attempt) {
        auto const delta{vec3<int64_t>{position} - vec3<int64_t>{person.position}};
        if(std::max({std::abs(delta.x), std::abs(delta.y), std::abs(delta.z)}) < 524'288) break;
        position.x = maths::dword(static_cast<int64_t>(position.x) - (delta.x >> 1));
        position.y = maths::dword(static_cast<int64_t>(position.y) - (delta.y >> 1));
      }
      position.z = 50;
      if(auto const van{weapons.spawn(world::object_type::rescue_van, position)}) {
        theatre.objects[*van].flags |= 0x7000u;
        notice = "Rescue van called";
      }
    }
    break;
  case action::toggle_remote:
    if(mode == player_mode::on_foot && controlled_aircraft) {
      remote = !remote;
      notice = remote ? "Remote flight controls on" : "Remote flight controls off";
    }
    break;
  default:
    break;
  }
}

void session::find_surfaces() {
  /// Original support surfaces use world-aligned collision extents even when their visual models rotate
  landing.surface_height = 0;
  landing.runway = false;
  person.surface_height = 0;
  for(auto const &object : theatre.objects) {
    if(!object.has(world::object_flag::present)) continue;
    auto const &definition{geometry.definition(object.type)};
    auto within_xy{[&](maths::world_position const &point){
      return std::abs(static_cast<int64_t>(point.x) - object.position.x) < object.collision_extents.x
        && std::abs(static_cast<int64_t>(point.y) - object.position.y) < object.collision_extents.y;
    }};
    if(has(definition, world::object_property::runway) && within_xy(aircraft.position)
       && std::abs(static_cast<int64_t>(aircraft.position.z) - object.position.z) < object.collision_extents.z) {
      landing.surface_height = object.position.z;
      landing.runway = true;
    }
    if(mode != player_mode::on_foot || object.type == world::object_type::crater || !within_xy(person.position)) continue;
    if(std::abs(static_cast<int64_t>(person.position.z) - object.position.z) >= object.collision_extents.z) continue;
    if(has(definition, world::object_property::kills_person) && !invincible) {
      fatal_contact = object.type;
      mode = player_mode::dead;
      notice = "Killed on foot";
    } else if(has(definition, world::object_property::captures_person) && !invincible) {
      mode = player_mode::captured;
      notice = "Captured";
    } else if(has(definition, world::object_property::walkable)) {
      int32_t height{object.position.z};
      if(has(definition, world::object_property::raised_walking_surface)) height += object.collision_extents.z - flight::landing_state::aircraft_height - 1;
      if(person.position.z + flight::landing_state::aircraft_height >= height) person.surface_height = height;
    }
  }
}

void session::enter_portal(world::placed_object &portal, flight::time_step const step) {
  /// XPTIM records a transfer for the following frame; damage/repair and one-time objective credit happen on contact
  if(mode != player_mode::in_aircraft) return;
  auto const &parameters{resources::portal_qualities[portal.quality()]};
  uint8_t const transfer{static_cast<uint8_t>(parameters.action_bits)};
  pending_portal = transfer == 0 ? portal_transfer::none : transfer >= 128 ? portal_transfer::random : portal_transfer::home;
  if((parameters.action_bits & 0x100u) != 0) weapons.complete(portal);
  portal.timer &= 16'383u;
  if(parameters.damage_or_repair < 0) {
    mode = player_mode::dead;
    fatal_contact = portal.type;
    notice = "Fatal portal";
  } else if((parameters.damage_or_repair & 2) != 0) {
    auto const worst{damage.worst};
    damage = {};
    damage.worst = worst;
    damage.invincible = invincible;
  } else if(parameters.damage_or_repair != 0) flight::apply_damage(damage, aircraft, 75, random, step);
}

void session::check_aircraft_collisions(flight::time_step const step) {
  /// QCRASH tests the player's point against unrotated object boxes and treats runways and portals specially
  if(!controlled_aircraft) return;
  for(unsigned int i{0}; i != theatre.objects.size(); ++i) {
    auto &object{theatre.objects[i]};
    if(i == *controlled_aircraft || !object.has(world::object_flag::present) || !object.has(world::object_flag::player_contact)) continue;
    auto const &definition{geometry.definition(object.type)};
    bool inside{true};
    for(unsigned int axis{0}; axis != 3; ++axis) {
      if(std::abs(static_cast<int64_t>(aircraft.position[axis]) - object.position[axis]) >= static_cast<uint16_t>(object.collision_extents[axis])) inside = false;
    }
    if(!inside || object.type == world::object_type::crater || has(definition, world::object_property::runway)) continue;
    if(has(definition, world::object_property::portal)) {
      enter_portal(object, step);
      continue;
    }
    auto const attacker{object};
    fatal_contact = object.type;
    bool const fatal_orb{object.type == world::object_type::generator_orb && object.quality() == 4};
    if(has(definition, world::object_property::collision_callback)) weapons.damage(i, attacker, true);
    else if(object.has(world::object_flag::collision_callback)) object.timer = 0;
    unsigned int hits{attacker.has(world::object_flag::heavy_attack) ? 75u : attacker.has(world::object_flag::six_hit_attack) ? 6u : 1u};
    if(attacker.has(world::object_flag::heavy_attack)) {
      if(damage.total > hits) continue;
      aircraft.velocity.x = static_cast<int16_t>(maths::negate(aircraft.velocity.x) >> 1);
      aircraft.velocity.y = static_cast<int16_t>(maths::negate(aircraft.velocity.y) >> 1);
    }
    if(has(definition, world::object_property::forcefield)) {
      if(fast_ticks - last_wall_damage < 200) continue;
      last_wall_damage = fast_ticks;
      hits *= 2;
    }
    flight::apply_damage(damage, aircraft, hits, random, step);
    notice = "Aircraft damaged - E ejects";
    if(fatal_orb && mode == player_mode::in_aircraft && !damage.invincible) {
      airframe_destroyed = true;
      ++aircraft_losses;
      mode = player_mode::dead;
      notice = "Fatal impact - objective complete";
    }
  }
}

void session::enter_aircraft(unsigned int const index) {
  /// Entering a parked aircraft adopts its pose and variant and resets the original flight state
  auto &object{theatre.objects.at(index)};
  if(controlled_aircraft == index) {
    object.flags &= static_cast<uint16_t>(~std::to_underlying(world::object_flag::present));
    mode = player_mode::in_aircraft;
    remote = false;
    cockpit = true;
    notice = "Aircraft re-entered";
    return;
  }
  weapons.rearm();
  if(controlled_aircraft) theatre.objects[*controlled_aircraft].aircraft_parking_flags |= aircraft_parking::keep_pose;
  controlled_aircraft = index;
  aircraft = flight::aircraft_state{};
  rocket_timer = {
    .previous_tick{static_cast<uint16_t>(fast_ticks >> 6)},
  };
  rocket_requested = false;
  aircraft.position = maths::world_position{object.position};
  aircraft.attitude = object.attitude;
  aircraft.variant = (object.flags & 0x2000u) != 0 ? flight::aircraft_variant::experimental
    : (object.flags & 0x1000u) != 0 ? flight::aircraft_variant::reserve : flight::aircraft_variant::standard;
  aircraft.world_to_body = maths::make_rotation(aircraft.attitude);
  aircraft.body_to_world = maths::make_inverse_rotation(aircraft.attitude);
  aircraft.on_ground = true;
  angular = {
    .beginner_flight{beginner_flight},
  };
  landing = {};
  damage = {};
  damage.invincible = invincible;
  vision = {};
  airframe_destroyed = false;
  left_ground = false;
  if((object.flags & 0x104u) != 0) {
    landing.crash_landing = true;
    landing.stopped = true;
    damage.maximum_throttle = 0;
  }
  object.flags &= static_cast<uint16_t>(~std::to_underlying(world::object_flag::present));
  mode = player_mode::in_aircraft;
  remote = false;
  cockpit = true;
  notice = "Aircraft boarded - +/- throttle, arrows fly, Z/X rudder, F flaps, E exits";
}

void session::check_aircraft_entry() {
  /// The retained aircraft needs a four-thousand-centimetre departure before a two-thousand-centimetre re-entry
  if(mode != player_mode::on_foot) return;
  if(controlled_aircraft) {
    if(auto const distance{interaction_range(aircraft.position, person.position)}; distance && *distance > reentry_departure_distance) reentry_allowed = true;
  }
  for(unsigned int i{0}; i != theatre.objects.size(); ++i) {
    auto const &object{theatre.objects[i]};
    if(object.type != world::object_type::aircraft || !object.has(world::object_flag::present)) continue;
    if(controlled_aircraft == i && !reentry_allowed) continue;
    if(auto const distance{interaction_range(object.position, person.position)}; distance && *distance <= contact_distance) {
      enter_aircraft(i);
      return;
    }
  }
}

void session::retain_aircraft() {
  /// Keep an abandoned aircraft in the same world while its independent flight state continues to advance
  if(!controlled_aircraft) return;
  auto &object{theatre.objects.at(*controlled_aircraft)};
  object.position = maths::world_position{aircraft.position};
  object.attitude = aircraft.attitude;
}

void session::leave_aircraft() {
  /// E steps onto the support surface when grounded and otherwise transfers flight velocity to the falling pilot
  if(!controlled_aircraft) return;
  rocket_requested = false;
  weapons.rearm_person();
  person.position = maths::world_position{aircraft.position};
  person.attitude = aircraft.attitude;
  person.ejection_attitude = aircraft.attitude;
  person.swing_frames = 0;
  vision.restore();
  person.velocity = maths::fixed_vector{aircraft.velocity};
  person.standing = aircraft.on_ground || landing.crash_landing || landing.stopped;
  person.parachute = false;
  person.jump_charge = 0;
  person.horizontal_damping = 32'000;
  if(person.standing) {
    // step just behind the wing on the left, using heading alone to stay on the support surface
    maths::angles const heading{.heading{aircraft.attitude.heading}};
    int16_t const aft{aircraft.variant == flight::aircraft_variant::experimental ? experimental_exit_aft : aircraft_exit_aft};
    auto const offset{maths::transform(maths::make_inverse_rotation(heading), {static_cast<int16_t>(-aft), aircraft_exit_side, 0})};
    for(unsigned int axis{0}; axis < 2; ++axis)
      person.position[axis] = maths::dword(static_cast<int64_t>(person.position[axis]) + offset[axis]);
    person.position.z = landing.surface_height + flight::landing_state::aircraft_height;
    person.velocity = maths::fixed_vector{0, 0, 0};
  }
  retain_aircraft();
  theatre.objects.at(*controlled_aircraft).flags |= std::to_underlying(world::object_flag::present);
  reentry_allowed = false;
  mode = player_mode::on_foot;
  remote = false;
  if(!person.standing) {
    sortie_flags |= std::to_underlying(sortie_status::ejected);
    person.tumble.x = maths::word((random.below(8192) & 0xefffu) | 0x800u);
    person.tumble.y = maths::word((random.below(8192) & 0xefffu) | 0x800u);
  } else person.tumble = vec2<int16_t>{0, 0};
  look_mode = looking = false;
  head_angles = {};
  notice = person.standing ? "On foot - walk over 40m away, then return to board" : "Ejected - open your parachute; F4 shows controls";
}

void session::update_aircraft(input_state const &input, flight::time_step const step) {
  /// Preserve the original ordering: old axes feed control/forces, then landing precedes position integration
  if(!controlled_aircraft || airframe_destroyed) return;
  if(recording && mode == player_mode::in_aircraft) return;
  unsigned int const pressure_target{static_cast<unsigned int>(aircraft.throttle) * 32'768 / 200 * aircraft.power_factor >> 16};
  aircraft.oil_pressure = static_cast<uint16_t>(aircraft.oil_pressure + (static_cast<int>(pressure_target) - aircraft.oil_pressure) / 40);
  if(landing.stopped) return;
  unsigned int const temperature_target{static_cast<unsigned int>(aircraft.throttle) * 32'768 / 200};
  aircraft.engine_temperature = static_cast<uint16_t>(aircraft.engine_temperature + (static_cast<int>(temperature_target) - aircraft.engine_temperature) / 180);
  aircraft.world_to_body = maths::make_rotation(aircraft.attitude);
  aircraft.body_to_world = maths::make_inverse_rotation(aircraft.attitude);
  flight::control_input const neutral;
  auto const &stick{mode == player_mode::in_aircraft || remote ? input.stick : neutral};
  if(!landing.crash_landing) flight::advance_controls(aircraft, angular, stick, atmosphere, step, &random);
  flight::advance_forces(aircraft, atmosphere, step);
  int32_t const altitude{aircraft.position.z - landing.surface_height};
  bool fatal{altitude < 0};
  if(!fatal && altitude < flight::landing_state::aircraft_height + flight::landing_state::half_wheel_span) fatal = !flight::find_wheel_height(aircraft, landing);
  auto contact{fatal ? flight::landing_result::fatal : flight::advance_landing(aircraft, angular, landing, stick, step)};
  if(invincible && (contact == flight::landing_result::fatal || contact == flight::landing_result::crash_landing)) {
    flight::recover_invulnerable_aircraft(aircraft, landing);
    contact = flight::landing_result::airborne;
  }
  if(contact == flight::landing_result::fatal) {
    if(!airframe_destroyed) ++aircraft_losses;
    airframe_destroyed = true;
    landing.crash_landing = true;
    landing.stopped = true;
    aircraft.throttle = 20;
    if(mode == player_mode::in_aircraft) {
      mode = player_mode::dead;
      notice = "Fatal crash";
    }
  } else if(contact == flight::landing_result::crash_landing) {
    aircraft.throttle = 20;
    aircraft.attitude.pitch = 0;
    aircraft.attitude.roll = 0;
    aircraft.velocity.z = 0;
    aircraft.velocity.x = maths::word((static_cast<int32_t>(aircraft.velocity.x) * 30'000 >> 16) * 2);
    aircraft.velocity.y = maths::word((static_cast<int32_t>(aircraft.velocity.y) * 30'000 >> 16) * 2);
    aircraft.attitude.heading += static_cast<maths::angle>(std::abs(static_cast<int>(aircraft.velocity.x)) + std::abs(static_cast<int>(aircraft.velocity.y)));
    aircraft.position.z = landing.surface_height + flight::landing_state::aircraft_height;
    if(std::abs(static_cast<int>(aircraft.velocity.x)) + std::abs(static_cast<int>(aircraft.velocity.y)) <= 200) landing.stopped = true;
    notice = "Crash landed - E to leave the aircraft";
  }
  if(!aircraft.on_ground) left_ground = true;
  if(left_ground && aircraft.on_ground && landing.stopped) {
    sortie_flags |= std::to_underlying(landing.crash_landing ? at_home() ? sortie_status::crashed_home : sortie_status::crashed_away : sortie_status::landed);
  }
  if(!landing.stopped) flight::advance_position(aircraft, step);
  retain_aircraft();
}

void session::update_scenery() {
  /// Apply nearby permanent handlers while the session owns tower handoffs and one-time objective credit
  if(theatre.expansion && at_home()) world::select_airfield(theatre, world::nearest_airfield(theatre, camera().position));
  for(unsigned int i{0}; i != theatre.objects.size(); ++i) {
    auto &object{theatre.objects[i]};
    auto const observer{camera().position};
    if(!object.has(world::object_flag::present) || !object.has(world::object_flag::updates)) continue;
    if(object.type == world::object_type::aircraft && !world::in_active_neighbourhood(object.position, observer)) continue;
    if(object.type == world::object_type::aircraft && controlled_aircraft != i) {
      if(advance_parked_aircraft(object, theatre.start, observer, parking, mode == player_mode::on_foot)) enter_aircraft(i);
    }
    switch(advance_scenery(object, observer, mode == player_mode::on_foot, fast_ticks)) {
    case scenery_event::contact:
      weapons.complete(object);
      notice = "Contact established - objective complete";
      break;
    case scenery_event::tower:
      clock.suspend();
      tower_request = visiting_tower = true;
      notice = "Airbase intelligence";
      break;
    case scenery_event::none:
      break;
    }
  }
}

void session::update_person(input_state const &input, flight::time_step const step) {
  /// Walking and free fall retain their own movement state while the abandoned aircraft still simulates
  if(!remote) {
    person.tumble.x = maths::word(static_cast<int32_t>(person.tumble.x) * 9500 / 10'100);
    person.tumble.y = maths::word(static_cast<int32_t>(person.tumble.y) * 9500 / 10'100);
    // the original adds tumble.x to AX after copying yaw into CX; only the second component actually changes the view
    int16_t const yaw{maths::word(-input.stick.roll)}, pitch{maths::word(-input.stick.pitch + person.tumble.y)};
    person.attitude.heading += static_cast<maths::angle>(timed_word(static_cast<int16_t>(yaw >> 1), step) * 2);
    person.attitude.pitch += static_cast<maths::angle>(timed_word(static_cast<int16_t>(pitch >> 1), step) * 2);
    int16_t const roll{std::bit_cast<int16_t>(person.attitude.roll)};
    person.attitude.roll += static_cast<maths::angle>(timed_word(static_cast<int16_t>(maths::negate(roll) >> 6), step));
    swing_parachute(person, input.stick);
  }
  if(!remote) {
    person.attitude.heading += static_cast<maths::angle>(input.look_yaw);
    int const pitch{std::clamp(static_cast<int>(maths::word(person.attitude.pitch)) + input.look_pitch, -16000, 16000)};
    person.attitude.pitch = static_cast<maths::angle>(pitch);
  }
  int const forward{static_cast<int>(input.walk_forward) - static_cast<int>(input.walk_backward)};
  int const sideways{input.strafe};
  if(forward != 0 || sideways != 0) {
    double const normalise{forward != 0 && sideways != 0 ? 0.7071067811865475 : 1.0};
    maths::angles const heading{.heading{person.attitude.heading}};
    auto const motion{maths::transform(maths::make_inverse_rotation(heading), {static_cast<int16_t>(forward * 25 * normalise), static_cast<int16_t>(sideways * 25 * normalise), 0})};
    person.position.x += maths::rounded_divide(static_cast<int32_t>(motion.x) * step.elapsed, step.normal);
    person.position.y += maths::rounded_divide(static_cast<int32_t>(motion.y) * step.elapsed, step.normal);
  }
  if(input.jump && person.standing && !person.charging_jump) {
    person.charging_jump = true;
    person.velocity.z = 4000;
    person.standing = false;
    person.parachute = false;
    person.horizontal_damping = 31000;
  }
  if(!input.jump) person.charging_jump = false;
  if(input.drop_through) person.surface_height = 0;
  int32_t const floor{person.surface_height + flight::landing_state::aircraft_height};
  if(person.position.z <= floor && person.velocity.z <= 0) {
    person.position.z = floor;
    person.velocity.z = 0;
    person.swing_frames = 0;
    person.standing = true;
    return;
  }
  person.standing = false;
  for(unsigned int axis{0}; axis != 3; ++axis) person.position[axis] += maths::rounded_divide(maths::scale(person.velocity[axis] >> 4, step.elapsed), step.normal);
  person.velocity.z = maths::word(person.velocity.z - timed_word(50, step));
  if(person.parachute) person.horizontal_damping = 30'000;
  damp_fall_velocity(person.velocity, person.horizontal_damping, person.parachute ? 900 : 4800, step);
  if(person.position.z < floor) {
    int const descent{-static_cast<int>(person.velocity.z)};
    if(descent >= 1800 && (person.parachute || descent >= 3200) && !invincible) {
      mode = player_mode::dead;
      notice = "Fatal fall";
    } else {
      person.position.z = floor;
      person.velocity.z = 0;
      person.standing = true;
    }
  }
}

void session::advance(input_state const &requested, std::optional<std::function_ref<void()>> const draw_frame) {
  input_state input{requested};
  if(course) input.weapons = {};
  if(recording && !paused && !visiting_tower) {
    auto const frames{recording->frames()};
    if(recorded_index >= frames.size()) { recording_complete = true; return; }
    auto const &record{frames[recorded_index]};
    input = {};
    input.weapons.gun = (record.buttons & 1u) == 0;
    input.weapons.missile = (record.buttons & 2u) == 0;
    aircraft.flaps = (record.buttons & 16u) != 0;
    if(recorded_ticks == 0) {
      if((record.buttons & 4u) != 0) press(action::drop_bomb);
      if((record.buttons & 32u) != 0 && mode == player_mode::in_aircraft) press(action::eject);
    }
    if(mode == player_mode::in_aircraft && (record.buttons & 32u) == 0) {
      aircraft.position = maths::world_position{record.position};
      aircraft.attitude = record.attitude;
      aircraft.world_to_body = maths::make_rotation(record.attitude);
      aircraft.body_to_world = maths::make_inverse_rotation(record.attitude);
      for(unsigned int axis{0}; axis != 3; ++axis) aircraft.velocity[axis] = maths::word(static_cast<int32_t>(maths::word(record.velocity[axis])) / 2 * 16);
      aircraft.body_velocity = maths::transform(aircraft.world_to_body, aircraft.velocity);
      for(unsigned int axis{0}; axis != 3; ++axis) aircraft.scaled_body_velocity[axis] = static_cast<int16_t>(aircraft.body_velocity[axis] / 16);
      aircraft.throttle = 199;
    }
    // Original playback uses ten-unit frames; native flight advances in five-unit steps.
    if(++recorded_ticks == 2) { recorded_ticks = 0; ++recorded_index; }
  }
  if(builder) {
    frame = advance_frame(frame);
    builder->advance(input.stick, input.map, input.walk_forward, input.walk_backward);
    notice = builder->status();
    if(draw_frame) (*draw_frame)();
    return;
  }
  /// Fixed simulation steps advance forty fast PIT ticks, independently of rendered frames.
  if(map) {
    fast_ticks += timing::pit_ticks_per_step;
    clock.advance(timing::pit_ticks_per_step);
    map->advance(input.stick, input.map);
    if(draw_frame) (*draw_frame)();
    return;
  }
  if(paused || visiting_tower || finished()) return;
  walking = input.walk_forward || input.walk_backward || input.strafe != 0;
  if(input.throttle_up) press(action::throttle_up);
  if(input.throttle_down) press(action::throttle_down);
  fast_ticks += timing::pit_ticks_per_step;
  clock.advance(timing::pit_ticks_per_step);
  frame = advance_frame(frame);
  vision.advance(aircraft.body_force.z, mode != player_mode::in_aircraft, frame, {
    .elapsed{timing::physics_units_per_step},
    .normal{timing::physics_units_per_step},
  });
  for(auto &object : theatre.objects) switch(object.type) {
  case world::object_type::wire_forcefield_x:
  case world::object_type::wire_forcefield_y:
  case world::object_type::wire_forcefield_z:
  case world::object_type::wire_forcefield_z1:
    object.collision_extents = geometry.collision_extents(object.type, frame);
    break;
  default: break;
  }
  flight::time_step const step{
    .elapsed{timing::physics_units_per_step},
    .normal{timing::physics_units_per_step},
  };
  auto const observer{camera().position};
  auto const previous_objectives{weapons.stats().objectives_completed};
  // the original proximity channels consume the previous object pass before rebuilding their distances
  audible_proximity = enemies.proximity();
  weapons.move(observer, step, mode == player_mode::on_foot, fast_ticks);
  auto const &effects{enemies.advance({
    .observer{observer},
    .target{controlled_aircraft ? aircraft.position : person.position},
    .target_velocity{aircraft.velocity},
    .on_foot{mode == player_mode::on_foot},
    .timer{fast_ticks},
  }, step)};
  if(effects.parking != parking_reset::unchanged) {
    parking.resetting = effects.parking == parking_reset::enable;
    if(parking.resetting) parking.placed_count = 0;
  }
  orb_distortion = effects.orb_distortion;
  if(mode == player_mode::on_foot && effects.capture_closing) vision.close_capture(step);
  for(unsigned int axis{0}; axis != 3; ++axis) {
    aircraft.velocity[axis] = maths::word(aircraft.velocity[axis] + effects.attraction[axis]);
    std::array<int16_t, 3> constexpr rates{310, 186, 242};
    palette_phase[axis] += static_cast<uint16_t>(maths::multiply(timed_word(rates[axis], step), maths::word(effects.hallucination)));
  }
  if(std::ranges::none_of(theatre.objects, [](auto const &object){return object.type == world::object_type::mindbender && object.has(world::object_flag::present);}))
    palette_phase = vec3<uint16_t>{0, 0, 0};
  if(effects.rescued) {
    mode = player_mode::rescued;
    notice = "Rescued";
  }

  weapons.collisions(observer);
  update_scenery();
  if(draw_frame) (*draw_frame)();
  auto const transfer{std::exchange(pending_portal, portal_transfer::none)};
  if(transfer != portal_transfer::none) {
    transfer_portal(aircraft.position, theatre.start, transfer, random);
    orb_distortion = 0;
    enemies.refresh_census(aircraft.position);
    retain_aircraft();
  }
  if(controlled_aircraft && (theatre.expansion || (experimental_clearance && aircraft.variant == flight::aircraft_variant::experimental))
    && (aircraft.position.z >> 8) >= (theatre.expansion ? atmosphere.height / 2 : atmosphere.height * 3 / 5)) {
    mode = player_mode::earth_saved;
    notice = theatre.expansion ? "You have gone into orbit!" : "You have saved Earth from alien attack!";
  }
  find_surfaces();
  check_aircraft_collisions(step);
  angular.wing_damage = damage.wing;
  angular.rudder_damage = damage.rudder;
  angular.stability_damage = damage.stability;
  looking = look_mode || input.view.modifier;
  auto flight_input{input};
  if(mode == player_mode::on_foot && effects.capture_pending) {
    remote = false;
    auto const delta{effects.approach_target - person.position};
    auto const desired{maths::arctangent(maths::word(delta.x >> 6), maths::word(delta.y >> 6))};
    person.attitude.heading += static_cast<maths::angle>(timed_word(static_cast<int16_t>(maths::word(desired - person.attitude.heading) >> 7), step));
    maths::angles const direction{
      .heading{person.attitude.heading},
    };
    auto const stride{maths::transform(maths::make_inverse_rotation(direction), {timed_word(500, step), 0, 0})};
    person.position.x += maths::rounded_divide(maths::scale(stride.x >> 4, step.elapsed), step.normal);
    person.position.y += maths::rounded_divide(maths::scale(stride.y >> 4, step.elapsed), step.normal);
    flight_input.stick.roll = flight_input.stick.pitch = 0;
    flight_input.walk_forward = flight_input.walk_backward = false;
  }
  if(looking) {
    maths::angles desired;
    if(input.view.left) desired.heading = 16'380;
    if(input.view.right) desired.heading = static_cast<maths::angle>(-16'380);
    if(input.view.up) desired.pitch = static_cast<maths::angle>(-8192);
    if(input.view.down) {
      if(mode == player_mode::in_aircraft) desired.pitch = 8192;
      else desired.heading = 32'767;
    }
    if(input.view.below && mode == player_mode::in_aircraft) desired.pitch = 16'383;
    int const divisor{std::max(2, 2 * step.normal / step.elapsed)};
    head_angles.heading += static_cast<maths::angle>(maths::word(desired.heading - head_angles.heading) / divisor);
    head_angles.pitch += static_cast<maths::angle>(maths::word(desired.pitch - head_angles.pitch) / divisor);
    if(!input.joystick_controlled) flight_input.stick.roll = flight_input.stick.pitch = 0;
  } else head_angles = {};
  update_aircraft(flight_input, step);
  if(mode == player_mode::on_foot) {
    update_person(flight_input, step);
    check_aircraft_entry();
  }
  bool const rocket_ready{rocket_timer.advance(aircraft, static_cast<uint16_t>(fast_ticks >> 6), static_cast<uint16_t>(step.elapsed), static_cast<uint16_t>(step.normal))};
  if(rocket_ready && rocket_requested && !airframe_destroyed && (mode == player_mode::in_aircraft || remote)) {
    aircraft.rocket = true;
    rocket_requested = false;
  }
  bool const use_person{mode == player_mode::on_foot && !remote};
  auto const orientation{use_person ? maths::make_inverse_rotation(person.attitude) : aircraft.body_to_world};
  weapons.fire(input.weapons, {
    .position{use_person ? person.position : aircraft.position},
    .attitude{use_person ? person.attitude : aircraft.attitude},
    .body_to_world{orientation},
    .forward_airflow{aircraft.scaled_body_velocity.x},
    .on_foot{mode == player_mode::on_foot},
    .remote{remote},
    .crashed{landing.crash_landing},
  }, fast_ticks, step);
  particles.advance(random);
  bool const was_low{low_altitude};
  low_altitude = mode == player_mode::in_aircraft && !aircraft.on_ground && aircraft.position.z >= 0 && aircraft.position.z < (was_low ? 5000 : 4500);
  if(course && course->advance(camera().position, mode == player_mode::in_aircraft ? aircraft.on_ground : person.standing, at_home())) {
    // Keep the objective anchor explicit when replacing the assembly routine's implicit SI input.
    auto marker{std::ranges::find_if(theatre.objects, [](world::placed_object const &object){ return object.type == world::object_type::tet; })};
    if(marker != theatre.objects.end()) weapons.complete(*marker);
    notice = "Slalom course complete!";
  }
  if(messages && !finished()) {
    if(!course && frame == 7 && aircraft.throttle == 20) radio = theatre.training ? resources::radio_notice::training : resources::radio_notice::choose_mission;
    if(weapons.stats().objectives_completed > previous_objectives) radio = damage.total >= 16 ? resources::radio_notice::objective_damaged : resources::radio_notice::objective_complete;
    if(low_altitude && !was_low && aircraft.position.z >= 2000 && !at_home()) radio = resources::radio_notice::low_altitude;
    if(!steering_warned && mode == player_mode::in_aircraft && aircraft.on_ground && input.stick.roll != 0 && input.stick.rudder == 0) {
      radio = resources::radio_notice::ground_steering;
      steering_warned = true;
    }
  }
}

maths::random_source &session::random_stream() noexcept {
  /// Rendering and gameplay consume the same original RAND_W stream, once per simulation pass
  return random;
}

graphics::camera session::camera() const {
  if(builder) return builder->camera();
  /// Walking, cockpit and remote flight share one renderer but retain separate player and aircraft viewpoints
  if(map) {
    auto observer{map->camera()};
    observer.particles = &particles;
    return observer;
  }
  bool const on_foot{mode != player_mode::in_aircraft && (!controlled_aircraft || theatre.objects[*controlled_aircraft].has(world::object_flag::present))};
  auto attitude{on_foot ? person.attitude : aircraft.attitude};
  if(looking) attitude = maths::extract_angles(maths::rotate_local(maths::make_inverse_rotation(attitude), head_angles));
  return {
    .position{maths::world_position{on_foot ? person.position : aircraft.position}},
    .attitude{attitude},
    .cockpit{!on_foot && cockpit},
    .rear_view{!on_foot && rear},
    .ground_detail{ground_detail},
    .gunsight{mode == player_mode::in_aircraft || (mode == player_mode::on_foot && !walking)},
    .orb_distortion{orb_distortion},
    .palette_phase{vec3<uint16_t>{palette_phase}},
    .palette_brightness{vision.intensity()},
    .particles{&particles},
  };
}
world::theatre const &session::world() const noexcept {
  /// Expose the current live placements without transferring ownership
  return theatre;
}
player_mode session::player() const noexcept {
  /// Allow presentation and scripted checks to inspect locomotion without modifying simulation state
  return mode;
}
flight::aircraft_state const &session::craft() const noexcept {
  /// Report actual flight telemetry for the cockpit and scripted inspection
  return aircraft;
}
person_state const &session::pilot() const noexcept {
  /// Report the on-foot body independently of the remotely controlled aircraft
  return person;
}
std::string const &session::message() const noexcept {
  /// Keep transient game notices out of the console
  return notice;
}
void session::suspend_clock() noexcept {
  /// Front-end text/help/score screens replace the original external-program handoff and restart its elapsed-time segment
  clock.suspend();
}
uint32_t session::timer() const noexcept {
  /// Preserve the fast original timer used by flash colours and weapon delays
  return fast_ticks;
}
unsigned int session::frame_number() const noexcept {
  /// Retain the frame clock used by authored colour alternation and local-object timers
  return frame;
}
bool session::is_paused() const noexcept {
  /// Paused frames neither consume simulation time nor advance objective/weapon counters
  return paused;
}
bool session::messages_enabled() const noexcept {
  /// O toggles the original explanatory message boxes without changing simulation rules
  return messages;
}
std::optional<resources::radio_notice> session::take_radio() {
  /// Presentation consumes one original queued message while the core keeps no window or font state
  return std::exchange(radio, std::nullopt);
}
bool session::in_recording() const noexcept {
  return recording.has_value();
}
bool session::in_builder() const noexcept {
  return builder.has_value();
}
void session::begin_recording(std::span<uint8_t const> const data) {
  recording.emplace(data);
  recorded_index = recorded_ticks = 0;
  recording_complete = false;
  messages = false;
  if(!controlled_aircraft) {
    auto const found{std::ranges::find_if(theatre.objects, [](auto const &object){return object.type == world::object_type::aircraft && object.has(world::object_flag::present);})};
    if(found != theatre.objects.end()) enter_aircraft(static_cast<unsigned int>(found - theatre.objects.begin()));
    else if(auto const index{weapons.spawn(world::object_type::aircraft, theatre.start)}) enter_aircraft(*index);
  }
}
void session::begin_builder() {
  /// A builder copy remains frozen until saved; its original embedded definition is never modified.
  builder.emplace(theatre, geometry);
  invincible = true;
}
void session::edit(builder_command const command) {
  if(builder) builder->command(command);
}
void session::builder_speed(unsigned int const setting) {
  if(builder) builder->speed_setting(setting);
}
bool session::in_map() const noexcept {
  /// Map inspection freezes the sortie but continues the original independent input and blink clock
  return map.has_value();
}
bool session::looking_around() const noexcept {
  /// Num Lock or held Control directs the keypad at the pilot's view instead of the aircraft's control surfaces
  return looking;
}
bool session::autopilot_engaged() const noexcept {
  /// The indicator follows actual engagement, including the native low-speed disengagement
  return angular.autopilot.engaged;
}
flight::damage_state const &session::aircraft_damage() const noexcept {
  /// Report individual native damage channels for the instrument panel
  return damage;
}
sortie_statistics const &session::statistics() const noexcept {
  /// Expose earned totals without permitting the presentation layer to edit gameplay state
  return weapons.stats();
}
sound_cues const &session::sounds() const noexcept {
  /// Audio observes successful gameplay events without consuming or mutating them
  return weapons.sounds();
}
proximity_sound const &session::proximity_sounds() const noexcept {
  /// Audio uses the preceding actor pass, as in the original pre-DRAWNEAR sound block
  return audible_proximity;
}

uint8_t session::rocket_indicator_colour() const noexcept {
  /// Native recharge makes the oil-pressure needle blue while charging, green when ready and red during ignition
  return rocket_timer.colour;
}
bool session::low_altitude_warning() const noexcept {
  /// The panel warning retains hysteresis independently of whether explanatory radio boxes are enabled
  return low_altitude;
}
unsigned int session::bomb_supply() const noexcept {
  /// Cockpit annunciators follow the same finite supply and recharge timer as actual bomb releases
  return weapons.bomb_supply();
}
bool session::remote_control() const noexcept {
  /// Remote operation retains engine sound and control while the pilot remains on foot
  return remote;
}
bool session::at_home(uint16_t const maximum_distance) const {
  /// HOMEDETECT uses the nearby control tower's three-dimensional range at the original one-sixty-fourth scale
  auto const &position{(mode == player_mode::in_aircraft || (mode == player_mode::dead && airframe_destroyed)) ? aircraft.position : person.position};
  for(auto const &object : theatre.objects) {
    if(object.type != world::object_type::control_tower || !object.has(world::object_flag::present)) continue;
    maths::fixed_vector relative{0, 0, 0};
    bool nearby{true};
    for(unsigned int axis{0}; axis != 3; ++axis) {
      auto const delta{static_cast<int64_t>(object.position[axis]) - position[axis]};
      if(std::abs(delta) >= 1'048'576) nearby = false;
      relative[axis] = maths::word(static_cast<int32_t>(delta >> 6));
    }
    if(nearby && maths::magnitude(relative) <= maximum_distance) return true;
  }
  return false;
}
sortie_statistics session::results(bool const aborted) const {
  /// Combine combat credit with the pilot's actual present and historical sortie state
  sortie_statistics result{weapons.stats()};
  result.status |= sortie_flags;
  result.maximum_damage = damage.worst;
  result.aircraft_lost = aircraft_losses;
  result.contacting_type = std::to_underlying(fatal_contact);
  result.elapsed_seconds = clock.elapsed();
  if(at_home() || mode == player_mode::returned) result.mark(sortie_status::home);
  if(mode == player_mode::dead) {
    result.mark(sortie_status::killed);
  }
  if(mode == player_mode::captured) result.mark(sortie_status::captured);
  if(mode == player_mode::rescued) result.mark(sortie_status::rescued);
  if(mode == player_mode::earth_saved) result.mark(sortie_status::earth_saved);
  if(invincible) result.mark(sortie_status::invincible);
  if(aborted) result.mark(sortie_status::aborted);
  return result;
}
world::theatre session::snapshot() const {
  /// Preserve permanent live placements and return the retained aircraft to the saved world, excluding ephemeral particles and bullets
  world::theatre result;
  result.tiles = theatre.tiles;
  result.start = maths::world_position{theatre.start};
  if(theatre.expansion && at_home()) world::select_airfield(result, world::nearest_airfield(theatre, camera().position));
  result.sky = theatre.sky;
  result.environment = theatre.environment;
  result.briefing = theatre.briefing;
  result.missions = theatre.missions;
  result.training = theatre.training;
  for(unsigned int i{0}; i != theatre.objects.size(); ++i) {
    auto const &object{theatre.objects[i]};
    auto const &definition{geometry.definition(object.type)};
    if(i >= weapons.stored_objects() && (definition.properties & std::to_underlying(world::object_property::temporary)) != 0) continue;
    if(controlled_aircraft == i) {
      auto retained{object};
      retained.flags |= std::to_underlying(world::object_flag::present);
      retained.position = maths::world_position{aircraft.position};
      retained.attitude = aircraft.attitude;
      if(landing.crash_landing || airframe_destroyed) retained.flags = static_cast<uint16_t>((retained.flags | 0x104u) & ~8u);
      result.objects.push_back(std::move(retained));
    } else if(object.has(world::object_flag::present)) result.objects.push_back(object);
  }
  return result;
}
bool session::finished() const noexcept {
  if(recording_complete) return true;
  /// Terminal sortie states leave flight input and simulation to the result/front-end flow
  return mode != player_mode::on_foot && mode != player_mode::in_aircraft;
}
void session::relocate(world::mission_start const &start) {
  /// DONEWMISS retains pitch/roll, assigns the intelligence start and launches with 5000 forward airflow
  if(mode != player_mode::in_aircraft) {
    notice = "You must be in an aircraft to start in the air";
    return;
  }
  if(damage.total >= 16) {
    notice = "Aircraft too damaged for a mission jump";
    if(messages) radio = resources::radio_notice::jump_damaged;
    return;
  }
  aircraft.position = maths::world_position{start.position};
  aircraft.attitude.heading = start.heading;
  aircraft.world_to_body = maths::make_rotation(aircraft.attitude);
  aircraft.body_to_world = maths::make_inverse_rotation(aircraft.attitude);
  aircraft.body_velocity = maths::fixed_vector{5000, 0, 0};
  aircraft.scaled_body_velocity = maths::fixed_vector{312, 0, 0};
  aircraft.velocity = maths::transform(aircraft.body_to_world, aircraft.body_velocity);
  aircraft.throttle = damage.maximum_throttle;
  aircraft.on_ground = false;
  landing = {
    .stopped{false},
  };
  left_ground = true;
  retain_aircraft();
  enemies.refresh_census(aircraft.position);
  notice = start.title;
}
bool session::request_exit() {
  /// The first Escape away from base queues the original warning; a later Escape ends the sortie
  uint16_t constexpr warning_home_range{5200};
  if(messages && !at_home(warning_home_range) && !exit_warned) {
    exit_warned = true;
    radio = mode == player_mode::in_aircraft ? resources::radio_notice::leave_aircraft : resources::radio_notice::leave_on_foot;
    return false;
  }
  return true;
}
bool session::take_tower() {
  /// HUTTIM asks the unified front end for intelligence without ending or recording the current sortie
  return std::exchange(tower_request, false);
}
void session::resume_from_tower() {
  /// DOTOWER returns a walking pilot outside the doorway and repairs the retained aircraft before flight resumes
  if(!visiting_tower || tower_request) return;
  visiting_tower = false;
  person.position.y = maths::dword(static_cast<int64_t>(person.position.y) - 2000);
  person.attitude.heading = static_cast<maths::angle>(-16'384);
  person.velocity = maths::fixed_vector{0, 0, 0};
  person.tumble = vec2<int16_t>{0, 0};
  head_angles = {};
  if(controlled_aircraft && !airframe_destroyed) {
    auto &object{theatre.objects[*controlled_aircraft]};
    object.flags = 1;
    object.pose = world::model_pose::normal;
    aircraft.attitude.pitch = static_cast<maths::angle>(-1500);
    aircraft.position.z = flight::landing_state::aircraft_height;
    aircraft.world_to_body = maths::make_rotation(aircraft.attitude);
    aircraft.body_to_world = maths::make_inverse_rotation(aircraft.attitude);
    aircraft.rocket_ticks = rocket_charge::capacity;
    rocket_timer = {
      .previous_tick{static_cast<uint16_t>(fast_ticks >> 6)},
    };
    rocket_requested = false;
    landing.crash_landing = false;
    landing.stopped = true;
    retain_aircraft();
  }
  auto const worst{damage.worst};
  damage = {};
  damage.worst = worst;
  damage.invincible = invincible;
  angular.wing_damage = angular.rudder_damage = angular.stability_damage = 0;
  vision.restore();
  notice = "Returned from airbase intelligence";
}
void session::set_invincible(bool const value) noexcept {
  /// Training's invulnerability setting is the original damage immunity flag
  invincible = value;
  damage.invincible = value;
}
void session::set_beginner_flight(bool const enabled) noexcept {
  /// The original beginner aerodynamics adds gentle pitch-to-level feedback without changing the force model
  beginner_flight = enabled;
  angular.beginner_flight = enabled;
}
void session::set_experimental_clearance(bool const permitted) noexcept {
  /// Campaign rank grants access to the black experimental aircraft's throttle
  experimental_clearance = permitted;
}

} // namespace corncob::game

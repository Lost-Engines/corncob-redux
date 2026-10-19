#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include "maths/fixed.h"
#include "maths/continuous.h"

namespace corncob::world {

enum class object_type : uint16_t {
  generator_orb, mindbender, aircraft, radar_dish, radar_base,
  runway_piece, control_tower, ground_crew, shard_2, shard,
  incendiary_fragment, kla_site, ground_vehicle, vehicle_site,
  forcefield_z1, forcefield_x, forcefield_y, forcefield_z,
  puff, explosion, bomb, missile, aa_shell, aaa_site, kla,
  flotsam_1, flotsam_2, fuel_dump, internal_aircraft, propeller,
  bullets, survey_marker, crater, destroyed_fuel_dump, tower_doorway,
  runway_end, runway_end_1, fence_1, fence_1a, fence_1b,
  barrage_balloon, bee_swarm, mortar, destroyed_generator_orb,
  wire_forcefield_z1, wire_forcefield_x, wire_forcefield_y, wire_forcefield_z,
  rescue_van, shell, saucer, saucer_site, tet, objective_saucer,
  small_wire_forcefield_x, small_wire_forcefield_y, small_wire_forcefield_z,
  alien, portal, small_step_forcefield, filled_small_step_forcefield,
  count,
};

enum class object_property : uint16_t {
  temporary = 1,
  discard_when_far = 2,
  collision_callback = 4,
  forcefield = 8,
  runway = 16,
  portal = 32,
  walkable = 64,
  kills_person = 128,
  raised_walking_surface = 256,
  captures_person = 512,
};

enum class object_flag : uint16_t {
  present = 0x0001,
  collision_callback = 0x0008,
  updates = 0x0010,
  player_contact = 0x0020,
  six_hit_attack = 0x0040,
  heavy_attack = 0x0080,
  box_collision = 0x0100,
  objective_complete = 0x0200,
  special = 0x0800,
};

enum class model_pose {
  normal,
  damaged,
  gesture,
  shot,
  paved,
  yellow_explosion,
  red_explosion,
  radar_wreck,
  fuel_wreck,
  blast_crater,
  experimental_aircraft,
  revealed_forcefield,
  distant_hidden,
  editor_cursor,
};

struct motion_profile {
  maths::fixed_vector spin{0, 0, 0};
  int16_t dispersion{0};
  int16_t drag_divisor{0};
  int16_t gravity{0};
};

struct placed_object {
  maths::world_position position{0, 0, 0};
  maths::angles attitude;
  maths::fixed_vector velocity{0, 0, 0};
  object_type type{object_type::generator_orb};
  uint16_t flags{0};
  uint16_t timer{0};
  uint8_t health{0};
  uint8_t collision_group{0};
  motion_profile motion{};
  model_pose pose{model_pose::normal};
  std::optional<uint8_t> portal_colour{};
  std::optional<uint8_t> shard_colour{};
  uint8_t aircraft_parking_flags{0};
  maths::fixed_vector collision_extents{0, 0, 0};
  maths::motion_fraction fraction{};

  unsigned int quality() const noexcept {
    /// Qualities occupy three stored flag bits and index eight-row tuning tables
    return (flags >> 12) & 7u;
  }
  bool has(object_flag const flag) const noexcept {
    /// Common flag meanings apply independently of type-specific overloaded state bits
    return (flags & std::to_underlying(flag)) != 0;
  }
};

struct world_tile {
  unsigned int first_object{0};
  unsigned int object_count{0};
  uint8_t storage_flags{0};
  std::array<uint8_t, 5> packed_colours{};
};

struct sky_parameters {
  maths::angle sun_angle{8192};
  uint16_t first_sun_diameter{120};
  uint16_t second_sun_diameter{112};
  uint8_t sun_colour{14};
  bool stars{false};
};

struct environment_parameters {
  int16_t gravity{-48};
  uint16_t sea_level_density{5957};
  uint16_t atmosphere_height{5957};
};

struct mission_start {
  std::string title;
  maths::world_position position{0, 0, 0};
  maths::angle heading{0};
};

struct theatre {
  bool expansion{false};
  static unsigned int constexpr columns{8};
  static unsigned int constexpr rows{8};
  static int32_t constexpr tile_size{32 * 65'536};
  static int32_t constexpr origin{-128 * 65'536};
  std::array<world_tile, columns * rows> tiles{};
  std::vector<placed_object> objects;
  maths::world_position start{0, 0, 0};
  sky_parameters sky{};
  environment_parameters environment{};
  std::string briefing;
  std::vector<mission_start> missions;
  bool training{false};
  bool slalom{false};
};

bool in_active_neighbourhood(maths::world_position const &position, maths::world_position const &observer) noexcept;

theatre decode_theatre(std::span<uint8_t const> source);
std::vector<uint8_t> encode_theatre(theatre const &source);
void apply_briefing(theatre &theatre, std::span<uint8_t const> source);

} // namespace corncob::world

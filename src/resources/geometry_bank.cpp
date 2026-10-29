#include "geometry_bank.h"
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <utility>
#include "resources/binary_reader.h"
#include "resources/colour_rules.h"
#include "resources/source_models.h"

namespace corncob::resources {

void geometry_bank::bind_animation(model_address const address, unsigned int const primitive, unsigned int const line, unsigned int const vertex, unsigned int const axis, bool const diameter) {
  /// Resolve original pointer tables once, retaining typed indices so the bank remains safe to move or copy
  binary_reader const input{source};
  std::optional<model_pulse> pulse;
  if(diameter) {
    if(address == 0x6a93) pulse = model_pulse::upper_saucer;
    if(address == 0x6a9f) pulse = model_pulse::lower_saucer;
  } else {
    std::array<unsigned int, 3> constexpr tables{0x6314, 0x633e, 0x6374};
    for(unsigned int table{0}; table != tables.size(); ++table) {
      unsigned int const start{tables[table]};
      for(unsigned int i{0}; i != input.word(start); ++i) if(input.word(start + 4 + i * 2) == address) pulse = static_cast<model_pulse>(table);
    }
  }
  if(pulse) animation_bindings.push_back({
    .primitive{primitive},
    .line{line},
    .vertex{vertex},
    .axis{axis},
    .pulse{*pulse},
    .negative{input.signed_word(address) < 0},
    .diameter{diameter},
  });
}
unsigned int geometry_bank::load_primitive(unsigned int const kind, model_address const address) {
  /// Cache typed drawing payloads once while preserving their original shared addresses
  uint32_t const key{(kind << 16) | address};
  if(auto const found{primitive_ids.find(key)}; found != primitive_ids.end()) return found->second;
  binary_reader const input{source};
  unsigned int const count{input.word(address)};
  if(count > 4096) throw std::invalid_argument{"original model has an invalid primitive count"};
  unsigned int const index{static_cast<unsigned int>(primitives.size())};
  switch(kind) {
  case 8:
    {
      polygon shape{
        .vertices{},
        .colour{input.word(address + 2)},
      };
      for(unsigned int i{0}; i != count; ++i) {
        shape.vertices.push_back(input.vector(address + 4 + i * 6));
        for(unsigned int axis{0}; axis != 3; ++axis) bind_animation(static_cast<model_address>(address + 4 + i * 6 + axis * 2), index, 0, i, axis);
      }
      primitives.emplace_back(std::move(shape));
    }
    break;
  case 2:
    {
      wireframe shape;
      for(unsigned int i{0}; i != count; ++i) {
        unsigned int const line_address{input.word(address + 2 + i * 2)};
        unsigned int const segments{input.word(line_address)};
        if(segments > 4096) throw std::invalid_argument{"original model has an invalid polyline length"};
        polyline line{
          .vertices{},
          .colour{input.word(line_address + 2)},
        };
        for(unsigned int vertex{0}; vertex != segments + 1; ++vertex) {
          line.vertices.push_back(input.vector(line_address + 4 + vertex * 6));
          for(unsigned int axis{0}; axis != 3; ++axis) bind_animation(static_cast<model_address>(line_address + 4 + vertex * 6 + axis * 2), index, i, vertex, axis);
        }
        shape.lines.push_back(std::move(line));
      }
      primitives.emplace_back(std::move(shape));
    }
    break;
  case 4:
    {
      point_cloud shape;
      for(unsigned int i{0}; i != count; ++i) {
        unsigned int const point_address{address + 2 + i * 8};
        shape.points.push_back({
          .position{input.vector(point_address)},
          .colour{input.word(point_address + 6)},
        });
      }
      primitives.emplace_back(std::move(shape));
    }
    break;
  case 6:
    {
      disc_cloud shape;
      for(unsigned int i{0}; i != count; ++i) {
        unsigned int const disc_address{address + 2 + i * 10};
        bind_animation(static_cast<model_address>(disc_address + 6), index, 0, i, 0, true);
        if(auto const particle{particle_animation::binding(static_cast<uint16_t>(disc_address))}) particle_bindings.push_back({
          .primitive{index},
          .vertex{i},
          .particle{*particle},
        });
        shape.discs.push_back({
          .position{input.vector(disc_address)},
          .diameter{input.word(disc_address + 6)},
          .colour{input.word(disc_address + 8)},
        });
      }
      primitives.emplace_back(std::move(shape));
    }
    break;
  default:
    throw std::invalid_argument{"original model has an unsupported drawing opcode"};
  }
  primitive_ids.emplace(key, index);
  return index;
}

void geometry_bank::load_model(model_address const address) {
  /// Retain all eight authored painter lists, including repeated and direction-specific commands
  if(models.contains(address)) return;
  binary_reader const input{source};
  model result;
  unsigned int const count{address == 0 ? 0u : input.word(address + 6)};
  if(count > 256) throw std::invalid_argument{"original model has an invalid command count"};
  for(unsigned int octant{0}; octant != result.octants.size(); ++octant) {
    for(unsigned int i{0}; i != count; ++i) {
      unsigned int const command_address{address + 8 + (octant * count + i) * 4};
      unsigned int const opcode{input.word(command_address)};
      if(opcode == 0) continue;
      result.octants[octant].push_back({
        .primitive_index{load_primitive(opcode, input.word(command_address + 2))},
      });
    }
  }
  models.emplace(address, std::move(result));
}

geometry_bank::geometry_bank(std::span<uint8_t const> const source, bool const slalom) :
  source{source},
  slalom_models{slalom} {
  /// Decode the original data segment as resources; no original instructions are interpreted
  unsigned int constexpr template_table{0x2cca};
  binary_reader const input{source};
  if(source.size() > colours.size()) throw std::invalid_argument{"original model data exceeds its native address space"};
  std::ranges::fill(colours, 0);
  std::ranges::copy(source, colours.begin());
  for(unsigned int type{0}; type != definitions.size(); ++type) {
    unsigned int const address{input.word(template_table + type * 2)};
    if(input.word(address + 72) != type) throw std::invalid_argument{"original object template failed its type check"};
    definitions[type] = {
      .models{
        .near{input.word(address + 40)},
        .middle{input.word(address + 42)},
        .far{input.word(address + 44)},
      },
      .limits{
        .near_squared{input.dword(address + 28)},
        .middle_squared{input.dword(address + 32)},
        .far_squared{input.dword(address + 36)},
      },
      .collision_extents{input.vector(address + 66)},
      .flags{input.word(address + 24)},
      .properties{input.word(0x2d44 + type * 2)},
      .collision_group{input.byte(address + 19)},
      .rotate_below_tier{input.byte(address + 26)},
      .hit_points{input.byte(address + 27)},
      .initial_timer{input.word(address + 52)},
      .initial_altitude{input.signed_dword(address + 8)},
      .initial_attitude{
        .heading{input.word(address + 12)},
        .pitch{input.word(address + 14)},
        .roll{input.word(address + 16)},
      },
      .initial_velocity{input.vector(address + 46)},
      .motion{
        .spin{input.vector(address + 54)},
        .dispersion{input.signed_word(address + 60)},
        .drag_divisor{input.signed_word(address + 62)},
        .gravity{input.signed_word(address + 64)},
      },
    };
    load_model(definitions[type].models.near);
    load_model(definitions[type].models.middle);
    load_model(definitions[type].models.far);
  }
  for(auto const address : {0x6f71, 0x7099, 0x5e2a, 0x3d64, 0x3d3c, 0x47a1, 0x406a, 0x4132}) load_model(static_cast<model_address>(address));
  load_model(map_marker_model);
  load_model(sun_model);
  load_model(experimental_model);
  // the crew's eyes and mouth paint onto a camera-facing head disc in every pose
  auto &head{std::get<disc_cloud>(primitives.at(primitive_ids.at((6u << 16) | 0x6e19u)))};
  for(std::size_t i{1}; i < head.discs.size(); ++i) head.discs[i].surface_anchor.emplace(head.discs.front().position);
  auto &face{std::get<wireframe>(primitives.at(primitive_ids.at((2u << 16) | 0x6ea3u)))};
  face.lines.front().surface_anchor.emplace(head.discs.front().position);
  // the injured pose's blood backdrop belongs behind the head, including its billboard edges
  auto &blood{std::get<polygon>(primitives.at(primitive_ids.at((8u << 16) | 0x6e4fu)))};
  blood.surface_anchor.emplace(head.discs.front().position);
  // The chase-view pilot uses the original standing crew silhouette, seen from
  // behind. Copy its primitives so the red shirt and blank back of the head
  // cannot change ordinary crew, the spy or their authored animation states.
  auto player{models.at(definition(world::object_type::ground_crew).models.near)};
  std::unordered_map<unsigned int, unsigned int> player_primitives;
  for(auto &octant : player.octants) for(auto &command : octant) {
    auto const original{command.primitive_index};
    if(!player_primitives.contains(original)) {
      auto shape{primitives.at(original)};
      if(auto *body{std::get_if<polygon>(&shape)}; body && body->colour == 0x6d15) body->colour = 12;
      if(auto *discs{std::get_if<disc_cloud>(&shape)}) discs->discs.resize(1); // Head without eyes.
      if(auto *lines{std::get_if<wireframe>(&shape)}) lines->lines.erase(lines->lines.begin()); // Keep the belt, omit the mouth.
      player_primitives.emplace(original, static_cast<unsigned int>(primitives.size()));
      primitives.push_back(std::move(shape));
    }
    command.primitive_index = player_primitives.at(original);
  }
  models.emplace(player_model, std::move(player));
  // Data-only source branches use a reserved address range, independently of the original executable's resources.
  std::vector<uint8_t> recovered(source_model_base + source_model_bytes.size());
  std::ranges::copy(source_model_bytes, recovered.begin() + source_model_base);
  std::ranges::copy(source_model_bytes, colours.begin() + source_model_base);
  this->source = recovered;
  for(auto const address : {source_bmrobj, source_shellobj, source_streeobj, source_sstreeobj, source_tetobj, source_crtrobj}) load_model(address);
  this->source = source;
  if(slalom_models) {
    using enum world::object_type;
    auto &tree{definitions[std::to_underlying(shell)]};
    tree.models = {
      .near{source_shellobj},
      .middle{source_shellobj},
      .far{source_streeobj},
    };
    tree.collision_extents = maths::fixed_vector{8000, 8000, 8000};
    tree.limits = {
      .near_squared{977'625'289},
      .middle_squared{556'680'332},
      .far_squared{16'680'332},
    };
    auto &farm{definitions[std::to_underlying(saucer)]};
    farm.models.near = farm.models.middle = source_bmrobj;
    farm.flags = 0x1b9;
    farm.collision_group = 2;
    farm.collision_extents = maths::fixed_vector{3000, 2000, 1000};
    farm.motion = {};
    auto &marker{definitions[std::to_underlying(tet)]};
    marker.models.near = marker.models.middle = source_tetobj;
    marker.collision_extents = maths::fixed_vector{1732, 1732, 18'116};
    auto &crater_definition{definitions[std::to_underlying(world::object_type::crater)]};
    crater_definition.models.near = crater_definition.models.middle = source_crtrobj;
    auto &plane{definitions[std::to_underlying(saucer_site)]};
    auto const &aircraft_definition{definitions[std::to_underlying(aircraft)]};
    plane.models = {
      .near{aircraft_definition.models.middle},
      .middle{aircraft_definition.models.far},
      .far{aircraft_definition.models.far},
    };
    plane.limits = {
      .near_squared{977'625'289},
      .middle_squared{256'680'332},
      .far_squared{16'680'332},
    };
    plane.hit_points = 2;
    plane.rotate_below_tier = 3;
    plane.flags = 0xb9;
    plane.collision_group = 1;
    plane.collision_extents = maths::fixed_vector{1440, 1440, 1440};
    plane.properties = 0;
    plane.motion = {};
    plane.initial_timer = 32'000;
    for(auto const type : {forcefield_x, forcefield_y, forcefield_z, forcefield_z1}) {
      auto &definition{definitions[std::to_underlying(type)]};
      definition.models.far = definition.models.near;
    }
  }
  // Other Worlds' extra 5c86 model is the builder cursor: six coloured spheres around the local axes.
  disc_cloud cursor;
  std::array<maths::fixed_vector, 6> const positions{
    maths::fixed_vector{500, 0, 0}, maths::fixed_vector{-500, 0, 0}, maths::fixed_vector{0, 500, 0},
    maths::fixed_vector{0, -500, 0}, maths::fixed_vector{0, 0, 500}, maths::fixed_vector{0, 0, -500},
  };
  std::array<uint16_t, 6> constexpr colours{13, 4, 9, 14, 0, 15};
  for(unsigned int i{0}; i != positions.size(); ++i) cursor.discs.push_back({
    .position{maths::fixed_vector{positions[i]}},
    .diameter{250},
    .colour{colours[i]},
  });
  unsigned int const index{static_cast<unsigned int>(primitives.size())};
  primitives.emplace_back(std::move(cursor));
  model marker;
  for(auto &octant : marker.octants) octant.push_back({
    .primitive_index{index},
  });
  models.emplace(editor_cursor_model, std::move(marker));
}

object_definition const &geometry_bank::definition(world::object_type const type) const {
  /// Provide the original drawing, health and collision defaults for a typed world object
  return definitions.at(std::to_underlying(type));
}
model_levels geometry_bank::appearance(world::placed_object const &object) const {
  /// Typed visual states retain the model-pointer switches authored in the original object handlers
  if(object.pose == world::model_pose::experimental_aircraft) return {experimental_model, experimental_model, 0x4ee3};
  if(object.type == world::object_type::aircraft && (object.flags & 0x100u) != 0) return {0x47a1, 0x47a1, 0x4ee3};
  switch(object.pose) {
  case world::model_pose::editor_cursor: return {editor_cursor_model, editor_cursor_model, editor_cursor_model};
  case world::model_pose::damaged:
    if(object.type == world::object_type::aircraft || object.type == world::object_type::internal_aircraft) return {0x47a1, 0x47a1, 0x4ee3};
    if(object.type == world::object_type::generator_orb) return {0x3227, 0x3227, 0x3227};
    if(object.type == world::object_type::radar_dish || object.type == world::object_type::radar_base) return {0x406a, 0x4132, 0x4132};
    if(object.type == world::object_type::fuel_dump) return {0x3521, 0x3569, 0x3569};
    break;
  case world::model_pose::gesture: return {0x6f71, 0x6f71, 0x6f71};
  case world::model_pose::shot: return {0x7099, 0x7099, 0x7099};
  case world::model_pose::paved: return {0x5e2a, 0x5e2a, 0x5ff2};
  case world::model_pose::yellow_explosion: return {0x3d64, 0x3d64, 0x3d64};
  case world::model_pose::red_explosion: return {0x3d3c, 0x3d3c, 0x3d3c};
  case world::model_pose::radar_wreck: return {0x406a, 0x4132, 0x4132};
  case world::model_pose::fuel_wreck: return {0x3521, 0x3569, 0x3569};
  case world::model_pose::blast_crater: return {0x72c6, 0x72c6, 0x72c6};
  case world::model_pose::revealed_forcefield:
    {
      auto result{definition(object.type).models};
      result.far = result.near;
      return result;
    }
  case world::model_pose::distant_hidden:
    {
      auto result{definition(object.type).models};
      result.far = 0;
      return result;
    }
  default: break;
  }
  return definition(object.type).models;
}

std::span<draw_command const> geometry_bank::commands(model_address const address, unsigned int const octant) const {
  /// Expose the selected painter list without copying or sorting its primitives
  if(address == 0) return {};
  return models.at(address).octants.at(octant);
}
primitive const &geometry_bank::drawing(unsigned int const index) const {
  /// Access one already decoded primitive shared by the authored command lists
  return primitives.at(index);
}
uint8_t geometry_bank::colour(colour_reference const reference) const noexcept {
  /// A nonzero high byte marks a native colour-variable address rather than a literal index
  return (reference >> 8) != 0 ? colours[reference] : static_cast<uint8_t>(reference);
}
void geometry_bank::set_map_colours(uint16_t const timer) {
  /// SURVCLR blinks yellow survey markers against the map's retained ground colour
  colours[0x743b] = (timer & 1024u) == 0 ? 14 : 0;
  colours[0x5b30] = (timer & 512u) == 0 ? 0 : 15;
}
void geometry_bank::animate(unsigned int const frame) {
  /// Update shared animated vertices and saucer sizes once per rendered game frame, without re-decoding static models
  auto const pulse{model_animation::at(frame)};
  colours[0x743b] = 0;
  for(auto const &binding : animation_bindings) {
    int16_t const value{binding.negative ? maths::negate(pulse.value(binding.pulse)) : pulse.value(binding.pulse)};
    std::visit([&](auto &shape){
      using shape_type = std::decay_t<decltype(shape)>;
      if constexpr(std::is_same_v<shape_type, wireframe>) shape.lines[binding.line].vertices[binding.vertex][binding.axis] = value;
      else if constexpr(std::is_same_v<shape_type, polygon>) shape.vertices[binding.vertex][binding.axis] = value;
      else if constexpr(std::is_same_v<shape_type, disc_cloud>) shape.discs[binding.vertex].diameter = static_cast<uint16_t>(value);
    }, primitives[binding.primitive]);
  }
}
particle_animation geometry_bank::initial_particles() const {
  /// Each sortie owns its mutable particle animation while sharing the original immutable templates
  return particle_animation{source};
}
void geometry_bank::animate_particles(particle_animation const &state) {
  /// Shared model pointers bind to the same live disc geometry without changing their authored colour references
  for(auto const &binding : particle_bindings) {
    auto &target{std::get<disc_cloud>(primitives[binding.primitive]).discs[binding.vertex]};
    auto const &disc{state.discs()[binding.particle]};
    target.position = maths::fixed_vector{disc.position};
    target.diameter = disc.diameter;
  }
}
maths::fixed_vector geometry_bank::collision_extents(world::object_type const type, unsigned int const frame) const noexcept {
  /// Wire forcefields use the same native phase as their vertices; collision half-extents are twice the geometric half-size
  auto result{maths::fixed_vector{definitions[std::to_underlying(type)].collision_extents}};
  auto const pulse{model_animation::at(frame)};
  using enum world::object_type;
  switch(type) {
  case wire_forcefield_x: result.y = maths::word(pulse.wide_wall * 2); result.z = maths::word(pulse.small_wall * 2); break;
  case wire_forcefield_y: result.x = maths::word(pulse.wide_wall * 2); result.z = maths::word(pulse.small_wall * 2); break;
  case wire_forcefield_z: result.x = maths::word(pulse.wide_wall * 2); result.y = maths::word(pulse.square_wall * 2); break;
  case wire_forcefield_z1: result.x = result.y = maths::word(pulse.small_wall * 2); break;
  default: break;
  }
  return result;
}
void geometry_bank::configure_sun(world::sky_parameters const &sky) {
  /// READUNIVPARAMS changes the two authored sun discs and their shared colour byte
  auto const &command{models.at(sun_model).octants.front().front()};
  auto &sun{std::get<disc_cloud>(primitives.at(command.primitive_index))};
  sun.discs.at(0).diameter = sky.first_sun_diameter;
  sun.discs.at(1).diameter = sky.second_sun_diameter;
  colours[0x537d] = sky.sun_colour;
}

void geometry_bank::prepare_colours(world::placed_object const &object, unsigned int const frame, uint16_t const timer, uint8_t const random, bool const radar_active) {
  /// Apply the traced per-object quality, shared random and timer-based colour bindings.
  if(slalom_models && object.type == world::object_type::tet) colours[source_tetclr] = random;
  if(slalom_models) {
    colour_reference constexpr wing_colour_address{0x44ce};
    if(object.type == world::object_type::saucer_site) colours[wing_colour_address] = 0;
    else if(object.type == world::object_type::aircraft || object.type == world::object_type::internal_aircraft)
      colours[wing_colour_address] = (object.flags & 0x1000u) != 0 ? 6 : 1;
  }
  uint64_t const type_bit{uint64_t{1} << std::to_underlying(object.type)};
  for(auto const &rule : colour_rules) {
    if(rule.types != 0 && (rule.types & type_bit) == 0) continue;
    unsigned int value{0};
    switch(rule.kind) {
    case colour_rule_kind::random:
      value = random;
      break;
    case colour_rule_kind::alternate:
      value = rule.values[frame & 1u];
      break;
    case colour_rule_kind::radar:
      value = rule.values[radar_active && (timer & rule.timer_mask) != 0 ? 1 : 0];
      break;
    case colour_rule_kind::flag_blink:
      value = rule.values[(object.flags & rule.flag_mask) != 0 && (timer & rule.timer_mask) != 0 ? 1 : 0];
      break;
    case colour_rule_kind::quality:
    case colour_rule_kind::quality_random:
      value = rule.values[object.quality()];
      if((rule.kind == colour_rule_kind::quality_random && value >= 0x8000u) || (rule.random_types & type_bit) != 0) value = random;
      break;
    }
    colours[rule.address] = static_cast<uint8_t>(value);
  }
  if(object.shard_colour && (object.type == world::object_type::shard || object.type == world::object_type::shard_2)) {
    model_address constexpr shard_colour_address{0x71cf};
    colours[shard_colour_address] = *object.shard_colour;
  }
  if(object.type == world::object_type::portal && object.portal_colour) {
    model_address constexpr portal_colour_address{0x2dd3};
    colours[portal_colour_address] = *object.portal_colour;
  }
}

} // namespace corncob::resources

#include "mission_builder.h"
#include <algorithm>
#include <format>
#include <limits>
#include "maths/integer.h"
#include "resources/survey_types.h"

namespace corncob::game {

namespace {

int32_t constexpr maximum_stored_altitude{32 * 65'536 - 1};

} // anonymous namespace

mission_builder::mission_builder(world::theatre &world, resources::geometry_bank const &geometry) :
  world{world},
  geometry{geometry},
  observer{
    .position{world.start.x, world.start.y, 60'000},
    .attitude{
      .pitch{6000},
    },
    .cockpit{false},
    .rear_view{false},
    .ground_detail{false},
  } {
  /// The original builder freezes object handlers and edits the same compact scenery records used by flight.
}
void mission_builder::update_marker() {
  if(!marker) {
    marker = static_cast<unsigned int>(world.objects.size());
    world.objects.push_back({
      .position{},
      .attitude{},
      .type{world::object_type::survey_marker},
      .pose{world::model_pose::editor_cursor},
    });
  }
  auto &object{world.objects[*marker]};
  object.position = maths::world_position{selected ? world.objects[*selected].position : cursor};
  object.flags = cursor_visible ? std::to_underlying(world::object_flag::present) : 0;
}
void mission_builder::change_type(int const direction) {
  if(!selected) return;
  auto &object{world.objects[*selected]};
  unsigned int kind{std::to_underlying(object.type)};
  do {
    kind = (kind + std::to_underlying(world::object_type::count) + direction) % std::to_underlying(world::object_type::count);
  } while((geometry.definition(static_cast<world::object_type>(kind)).properties & std::to_underlying(world::object_property::temporary)) != 0);
  auto const &definition{geometry.definition(static_cast<world::object_type>(kind))};
  auto position{maths::world_position{object.position}};
  position.z = definition.initial_altitude;
  object = {
    .position{maths::world_position{position}},
    .attitude{definition.initial_attitude},
    .velocity{maths::fixed_vector{definition.initial_velocity}},
    .type{static_cast<world::object_type>(kind)},
    .flags{definition.flags},
    .timer{definition.initial_timer},
    .health{definition.hit_points},
    .collision_group{definition.collision_group},
    .motion{definition.motion},
  };
}
void mission_builder::command(builder_command const command) {
  auto &position{selected ? world.objects[*selected].position : cursor};
  switch(command) {
  case builder_command::cursor:
    if(selected) { cursor = maths::world_position{position}; selected.reset(); }
    else {
      auto const ahead{maths::transform(maths::make_inverse_rotation(observer.attitude), {20'000, 0, 0})};
      cursor = maths::world_position{observer.position + maths::world_position{ahead}};
    }
    cursor_visible = true;
    break;
  case builder_command::hide_cursor: selected.reset(); cursor_visible = false; break;
  case builder_command::create:
    if(!cursor_visible) this->command(builder_command::cursor);
    {
      auto const &definition{geometry.definition(world::object_type::generator_orb)};
      maths::world_position const created_position{
        std::clamp(position.x, world::theatre::origin, -world::theatre::origin - 1),
        std::clamp(position.y, world::theatre::origin, -world::theatre::origin - 1),
        std::clamp(position.z, 0, maximum_stored_altitude),
      };
      world.objects.push_back({
        .position{maths::world_position{created_position}},
        .attitude{},
        .type{world::object_type::generator_orb},
        .flags{definition.flags},
        .timer{definition.initial_timer},
        .health{definition.hit_points},
        .collision_group{definition.collision_group},
        .motion{definition.motion},
      });
      selected = static_cast<unsigned int>(world.objects.size() - 1);
    }
    break;
  case builder_command::select:
    {
      uint64_t nearest{std::numeric_limits<uint64_t>::max()};
      std::optional<unsigned int> found;
      for(unsigned int i{0}; i != world.objects.size(); ++i) {
        auto const &object{world.objects[i]};
        if(object.pose == world::model_pose::editor_cursor || !object.has(world::object_flag::present) || (geometry.definition(object.type).properties & 1u) != 0) continue;
        int64_t const dx{(static_cast<int64_t>(object.position.x) - cursor.x) / 256};
        int64_t const dy{(static_cast<int64_t>(object.position.y) - cursor.y) / 256};
        int64_t const dz{(static_cast<int64_t>(object.position.z) - cursor.z) / 256};
        uint64_t const distance{static_cast<uint64_t>(dx * dx + dy * dy + dz * dz)};
        if(distance < nearest) { found = i; nearest = distance; }
      }
      selected = found;
      cursor_visible = true;
    }
    break;
  case builder_command::erase: if(selected) { cursor = maths::world_position{position}; world.objects[*selected].flags = 0; selected.reset(); } break;
  case builder_command::next_type: change_type(1); break;
  case builder_command::previous_type: change_type(-1); break;
  case builder_command::next_quality: case builder_command::previous_quality:
    if(selected) {
      auto &object{world.objects[*selected]};
      unsigned int const quality{(object.quality() + (command == builder_command::next_quality ? 1 : 7)) & 7u};
      object.flags = static_cast<uint16_t>((object.flags & ~0x7000u) | quality << 12);
    }
    break;
  case builder_command::special: if(selected) world.objects[*selected].flags ^= std::to_underlying(world::object_flag::special); break;
  case builder_command::complete: if(selected) world.objects[*selected].flags ^= std::to_underlying(world::object_flag::objective_complete); break;
  case builder_command::level: observer.attitude.pitch = observer.attitude.roll = 0; break;
  case builder_command::lock_height: if(cursor_visible) position.z = height_lock; break;
  case builder_command::remember_height: if(cursor_visible) height_lock = position.z; break;
  case builder_command::map:
    overhead = !overhead;
    observer.attitude.pitch = overhead ? 16'384 : 6000;
    observer.position.z = overhead ? 210 * 65'536 : 60'000;
    break;
  }
  update_marker();
}
void mission_builder::speed_setting(unsigned int const setting) {
  speed = 64 << std::min(setting, 8u);
}
void mission_builder::advance(flight::control_input const &stick, map_input const &input, bool const forward, bool const backward, double const ratio) {
  auto &position{input.move_object && cursor_visible ? (selected ? world.objects[*selected].position : cursor) : observer.position};
  if(input.rotate_object && selected) {
    auto &angles{world.objects[*selected].attitude};
    maths::turn(angles.heading, -stick.roll*ratio, fraction.angles.x);
    maths::turn(angles.pitch, -stick.pitch*ratio, fraction.angles.y);
    return;
  }
  if(overhead) {
    maths::move(position, {-stick.pitch*std::max(1,observer.position.z >> 14)*ratio,
                           -stick.roll*std::max(1,observer.position.z >> 14)*ratio, 0}, fraction.position);
  } else if(input.move_object && cursor_visible) {
    maths::angles const level{.heading{observer.attitude.heading}};
    auto const movement{maths::transform(maths::make_inverse_rotation(level), {
      maths::word(-static_cast<int32_t>(stick.pitch) * speed / 512), maths::word(-static_cast<int32_t>(stick.roll) * speed / 512), 0})};
    maths::move(position, vec3<double>{movement}*ratio, fraction.position);
  } else {
    maths::turn(observer.attitude.heading, stick.roll*ratio, fraction.angles.x);
    maths::turn(observer.attitude.pitch, -stick.pitch*ratio, fraction.angles.y);
    auto const movement{maths::transform(maths::make_inverse_rotation(observer.attitude), {maths::word((forward - backward) * speed), 0, 0})};
    maths::move(position, vec3<double>{movement}*ratio, fraction.position);
  }
  int32_t const vertical{(input.higher - input.lower) * speed};
  int32_t const maximum_height{input.move_object && cursor_visible ? maximum_stored_altitude : 210 * 65'536};
  maths::move(position, {0,0,vertical*ratio}, fraction.position);
  position.z = std::clamp(position.z, 0, maximum_height);
  position.x = std::clamp(position.x, world::theatre::origin, -world::theatre::origin - 1);
  position.y = std::clamp(position.y, world::theatre::origin, -world::theatre::origin - 1);
  update_marker();
}
graphics::camera mission_builder::camera() const {
  return observer;
}
std::string mission_builder::status() const {
  auto const &position{selected ? world.objects[*selected].position : cursor_visible ? cursor : observer.position};
  std::string name{selected ? std::string{resources::survey_types[std::to_underlying(world.objects[*selected].type)].name} : "Cursor"};
  return std::format("{}  X {} Y {} Z {}{}", name, position.x, position.y, position.z,
    selected ? std::format("  Type {} Quality {}{}{}", std::to_underlying(world.objects[*selected].type), world.objects[*selected].quality(),
      world.objects[*selected].has(world::object_flag::special) ? " Special" : "", world.objects[*selected].has(world::object_flag::objective_complete) ? " Complete" : "") : "");
}
std::optional<unsigned int> mission_builder::selected_object() const noexcept {
  return selected;
}

} // namespace corncob::game

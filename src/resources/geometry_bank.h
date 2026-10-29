#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <variant>
#include <vector>
#include "maths/fixed.h"
#include "resources/model_animation.h"
#include "resources/particle_animation.h"
#include "world/world.h"

namespace corncob::resources {

using model_address = uint16_t;
using colour_reference = uint16_t;

struct model_levels {
  model_address near;
  model_address middle;
  model_address far;
};
struct distance_limits {
  uint32_t near_squared;
  uint32_t middle_squared;
  uint32_t far_squared;
};
struct object_definition {
  model_levels models;
  distance_limits limits;
  maths::fixed_vector collision_extents{0, 0, 0};
  uint16_t flags;
  uint16_t properties;
  uint8_t collision_group;
  uint8_t rotate_below_tier;
  uint8_t hit_points;
  uint16_t initial_timer;
  int32_t initial_altitude;
  maths::angles initial_attitude{};
  maths::fixed_vector initial_velocity{0, 0, 0};
  world::motion_profile motion;
};

struct polygon {
  std::vector<maths::fixed_vector> vertices;
  colour_reference colour;
  std::optional<maths::fixed_vector> surface_anchor{};
};
struct polyline {
  std::vector<maths::fixed_vector> vertices;
  colour_reference colour;
  std::optional<maths::fixed_vector> surface_anchor{};
};
struct wireframe {
  std::vector<polyline> lines;
};
struct coloured_point {
  maths::fixed_vector position{0, 0, 0};
  colour_reference colour;
};
struct point_cloud {
  std::vector<coloured_point> points;
};
struct coloured_disc {
  maths::fixed_vector position{0, 0, 0};
  uint16_t diameter;
  colour_reference colour;
  std::optional<maths::fixed_vector> surface_anchor{};
};
struct disc_cloud {
  std::vector<coloured_disc> discs;
};
using primitive = std::variant<polygon, wireframe, point_cloud, disc_cloud>;

struct draw_command {
  unsigned int primitive_index;
};
struct model {
  std::array<std::vector<draw_command>, 8> octants;
};

class geometry_bank {
private:
  std::span<uint8_t const> source;
  bool slalom_models;
  std::array<object_definition, std::to_underlying(world::object_type::count)> definitions;
  std::unordered_map<model_address, model> models;
  std::vector<primitive> primitives;
  std::unordered_map<uint32_t, unsigned int> primitive_ids;
  std::array<uint8_t, 65'536> colours;

  struct animation_binding {
    unsigned int primitive;
    unsigned int line;
    unsigned int vertex;
    unsigned int axis;
    model_pulse pulse;
    bool negative;
    bool diameter;
  };
  std::vector<animation_binding> animation_bindings;
  struct particle_binding {
    unsigned int primitive;
    unsigned int vertex;
    unsigned int particle;
  };
  std::vector<particle_binding> particle_bindings;
  void bind_animation(model_address address, unsigned int primitive, unsigned int line, unsigned int vertex, unsigned int axis, bool diameter = false);
  unsigned int load_primitive(unsigned int kind, model_address address);
  void load_model(model_address address);

public:
  static model_address constexpr player_model{0xfffe};
  static model_address constexpr editor_cursor_model{0xffff};
  static model_address constexpr map_marker_model{0x5b3e};
  static model_address constexpr sun_model{0x545c};
  static model_address constexpr experimental_model{0x6838};

  geometry_bank(std::span<uint8_t const> source, bool slalom = false);
  object_definition const &definition(world::object_type type) const;
  model_levels appearance(world::placed_object const &object) const;
  std::span<draw_command const> commands(model_address address, unsigned int octant) const;
  primitive const &drawing(unsigned int index) const;
  uint8_t colour(colour_reference reference) const noexcept;
  void animate(unsigned int frame);
  particle_animation initial_particles() const;
  void animate_particles(particle_animation const &state);
  void set_map_colours(uint16_t timer);
  maths::fixed_vector collision_extents(world::object_type type, unsigned int frame) const noexcept;
  void configure_sun(world::sky_parameters const &sky);
  void prepare_colours(world::placed_object const &object, unsigned int frame, uint16_t timer, uint8_t random, bool radar_active);
};

} // namespace corncob::resources

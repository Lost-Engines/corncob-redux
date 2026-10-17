#pragma once

#include <array>
#include <vector>
#include "game/combat.h"
#include "game/parked_aircraft.h"
#include "game/vehicle.h"
#include "maths/random.h"

namespace corncob::game {

using actor_census = std::array<unsigned int, std::to_underlying(world::object_type::count)>;

struct actor_context {
  maths::world_position const &observer;
  maths::world_position const &target;
  maths::fixed_vector const &target_velocity;
  bool on_foot;
  uint32_t timer;
};
struct proximity_sound {
  uint16_t orb_distance{63};
  uint16_t kla_distance{64};
  uint8_t kla_quality{0};
  uint16_t saucer_distance{256};
  uint8_t saucer_frequency{255};
};

struct actor_effects : vehicle_effects {
  parking_reset parking{parking_reset::unchanged};
  uint16_t hallucination{0};
  uint16_t orb_distortion{0};
  maths::fixed_vector attraction{0, 0, 0};
  proximity_sound sound{};
};

class actors {
private:
  resources::geometry_bank const &geometry;
  world::theatre &theatre;
  combat &weapons;
  maths::random_source &random;
  actor_census previous_counts{};
  unsigned int observed_aliens{0};
  actor_effects effects;
  std::vector<unsigned int> active_updates;
  bool census_ready{false};
  uint8_t last_kla_quality{0};

  actor_census census(maths::world_position const &centre) const;
  void update_launcher(world::placed_object &object, actor_context const &context, flight::time_step step);
  void update_kla(world::placed_object &object, actor_context const &context, flight::time_step step);
  void update_saucer(world::placed_object &object, actor_context const &context, flight::time_step step);
  void update_shell(unsigned int index, actor_context const &context, flight::time_step step);
  void update_aaa(world::placed_object &object, actor_context const &context, flight::time_step step);
  void update_vehicle(world::placed_object &object, actor_context const &context, flight::time_step step);
  void update_alien(world::placed_object &object, actor_context const &context, flight::time_step step);
  void update_balloon(world::placed_object &object, flight::time_step step);
  void update_orb(world::placed_object &object, actor_context const &context, flight::time_step step);
  void update_mortar(world::placed_object &object, actor_context const &context, flight::time_step step);

public:
  actors(resources::geometry_bank const &geometry, world::theatre &theatre, combat &weapons);
  proximity_sound const &proximity() const noexcept;
  void refresh_census(maths::world_position const &observer);
  actor_effects const &advance(actor_context const &context, flight::time_step step);
};

} // namespace corncob::game

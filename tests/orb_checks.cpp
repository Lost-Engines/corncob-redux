#include <iostream>
#include <stdexcept>
#include "game/actors.h"
#include "resources/assets.h"

int main() {
  namespace world = corncob::world;
  namespace game = corncob::game;
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset()};
  world::theatre theatre;
  auto const flags{geometry.definition(world::object_type::generator_orb).flags};
  theatre.objects.push_back({.position{0,0,0}, .attitude{}, .type{world::object_type::generator_orb}, .flags{static_cast<uint16_t>(flags | 0x1000u)}, .timer{32000}});
  theatre.objects.push_back({.position{4'000'000,4'000'000,0}, .attitude{}, .type{world::object_type::fuel_dump}, .flags{geometry.definition(world::object_type::fuel_dump).flags}});
  // A far orb must neither cancel the nearby orb's effects nor borrow its mission census.
  theatre.objects.push_back({.position{4'000'000,4'000'000,0}, .attitude{}, .type{world::object_type::generator_orb}, .flags{static_cast<uint16_t>(flags | 0x1000u)}, .timer{32000}});
  game::combat weapons{geometry, theatre};
  game::actors actors{geometry, theatre, weapons};
  corncob::maths::world_position const origin{0,0,720};
  corncob::maths::fixed_vector const velocity{0,0,0};
  auto const &effects{actors.advance({origin,origin,velocity,false,0}, {5,5})};
  if(!theatre.objects[0].has(world::object_flag::objective_complete)) throw std::runtime_error{"remote fuel prevented a local objective from completing"};
  if(theatre.objects[2].has(world::object_flag::objective_complete)) throw std::runtime_error{"remote objective borrowed the observer's census"};
  if(effects.orb_distortion == 0 || effects.sound.orb_distance >= 63) throw std::runtime_error{"distant orb cancelled nearby effects"};
  world::theatre ruined;
  ruined.objects.push_back({.position{4'000'000,4'000'000,40'000}, .attitude{}, .type{world::object_type::destroyed_generator_orb}, .flags{static_cast<uint16_t>(geometry.definition(world::object_type::destroyed_generator_orb).flags | 0x200u)}, .timer{32000}});
  game::combat ruins{geometry, ruined}; game::actors falling{geometry,ruined,ruins};
  for(unsigned int frame{0}; frame < 202; ++frame) falling.advance({origin,origin,velocity,false,frame*40}, {5,5});
  if(ruined.objects[0].position.z != 3100) throw std::runtime_error{"distant destroyed orb did not settle"};
  std::cout << "Local orb objectives and distant continuous animation passed\n";
}

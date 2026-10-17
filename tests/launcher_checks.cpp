#include <algorithm>
#include <iostream>
#include <stdexcept>
#include "game/actors.h"
#include "resources/assets.h"

int main() {
  namespace world = corncob::world;
  namespace game = corncob::game;
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset()};
  for(auto const type : {world::object_type::kla_site, world::object_type::vehicle_site, world::object_type::saucer_site}) {
    world::theatre theatre;
    auto const &definition{geometry.definition(type)};
    theatre.objects.push_back({.position{4'000'000,4'000'000,0}, .attitude{}, .type{type}, .flags{definition.flags}, .timer{0}});
    game::combat weapons{geometry, theatre};
    game::actors actors{geometry, theatre, weapons};
    corncob::maths::world_position const origin{0,0,0};
    corncob::maths::fixed_vector const velocity{0,0,0};
    auto const child{type == world::object_type::kla_site ? world::object_type::kla : type == world::object_type::vehicle_site ? world::object_type::ground_vehicle : world::object_type::saucer};
    auto population{[&]{return std::ranges::count_if(theatre.objects, [&](auto const &object){return object.type == child && object.has(world::object_flag::present);});}};
    for(unsigned int step{0}; step < 10000 && population() == 0; ++step) {
      theatre.objects[0].timer = 0;
      actors.advance({origin, origin, velocity, false, step * 40}, {5,5});
    }
    if(population() != 1) throw std::runtime_error{"remote site never produced an actor"};
    if(type == world::object_type::kla_site) {
      for(unsigned int n{0}; n < 20; ++n) { theatre.objects[0].timer = 0; actors.advance({origin,origin,velocity,false,n*40}, {5,5}); }
      if(population() != 1) throw std::runtime_error{"local production cap was bypassed"};
      auto const child_index{std::ranges::find_if(theatre.objects, [&](auto const &object){return object.type == child && object.has(world::object_flag::present);}) - theatre.objects.begin()};
      auto const timer{theatre.objects[static_cast<std::size_t>(child_index)].timer};
      weapons.move(origin, {5,5});
      if(theatre.objects[static_cast<std::size_t>(child_index)].timer != timer - 1) throw std::runtime_error{"distant KLA entered suspended animation"};
    }
  }
  std::cout << "Distant production, local caps and remote actor lifetimes passed\n";
}

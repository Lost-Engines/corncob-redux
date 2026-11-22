#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include "game/session.h"
#include "resources/assets.h"
#include "resources/binary_reader.h"
#include "world/training.h"

int main(int const argc, char const *const argv[]) {
  /// Compare complete generated native placements across counts, fixed/random qualities and PRNG seeds
  if(argc != 2) throw std::invalid_argument{"training_checks requires its native fixture"};
  std::ifstream input{argv[1], std::ios::binary};
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>{input}, {}};
  corncob::resources::binary_reader const data{bytes};
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset()};
  unsigned int offset{12};
  unsigned int objects{0};
  for(unsigned int trial{0}; trial != data.dword(8); ++trial) {
    corncob::world::training_setup const setup{
      .kla{
        .count{data.word(offset)},
        .quality{data.signed_word(offset + 6)},
      },
      .vehicles{
        .count{data.word(offset + 2)},
        .quality{data.signed_word(offset + 8)},
      },
      .aaa{
        .count{data.word(offset + 4)},
        .quality{data.signed_word(offset + 10)},
      },
    };
    auto const world{corncob::world::generate_training(geometry, setup, data.dword(offset + 12))};
    unsigned int const count{data.dword(offset + 16)};
    if(world.objects.size() != count) throw std::runtime_error{"training object count differs"};
    offset += 20;
    for(auto const &object : world.objects) {
      for(unsigned int axis{0}; axis != 3; ++axis) {
        if(object.position[axis] != data.signed_dword(offset + axis * 4) || object.velocity[axis] != data.signed_word(offset + 46 + axis * 2)) {
          throw std::runtime_error{"training position/velocity differs in case " + std::to_string(trial) + " object " + std::to_string(objects)};
        }
      }
      if(object.attitude.heading != data.word(offset + 12) || object.attitude.pitch != data.word(offset + 14) || object.attitude.roll != data.word(offset + 16)
         || object.flags != data.word(offset + 24) || object.timer != data.word(offset + 52) || std::to_underlying(object.type) != data.word(offset + 72)) {
        throw std::runtime_error{"training pose/type/flags/timer differs in case " + std::to_string(trial) + " type " + std::to_string(std::to_underlying(object.type))};
      }
      offset += 74;
      ++objects;
    }
  }
  if(offset != bytes.size()) throw std::runtime_error{"unconsumed training fixture bytes"};
  corncob::game::session flight{geometry, corncob::world::generate_training(geometry, {})};
  if(flight.player() != corncob::game::player_mode::in_aircraft || flight.craft().position.y != -8000) throw std::runtime_error{"training did not start aboard the runway aircraft"};
  auto theatre{corncob::world::decode_theatre(corncob::resources::morning_asset())};
  corncob::world::apply_briefing(theatre, corncob::resources::morning_briefing_asset());
  if(theatre.missions.empty() || theatre.missions.front().title != "Insult to Injury") throw std::runtime_error{"original intelligence starts were not decoded"};
  auto const start{theatre.missions.front()};
  flight.relocate(start);
  if(flight.craft().position != start.position || flight.craft().attitude.heading != start.heading || flight.craft().body_velocity.x != 5000
     || flight.craft().on_ground || flight.craft().throttle != 200) throw std::runtime_error{"mission start did not reproduce DONEWMISS launch state"};
  auto const objects_before{flight.world().objects.size()};
  flight.advance({});
  if(flight.craft().position == start.position) throw std::runtime_error{"airborne mission start retained the stopped-runway latch"};
  if(flight.world().objects.size() != objects_before) throw std::runtime_error{"mission start recreated the live world"};
  flight.press(corncob::game::action::toggle_messages);
  flight.relocate({
    .title{"Low-altitude warning without radio boxes"},
    .position{0, 0, 4000},
    .heading{0},
  });
  flight.advance({});
  if(!flight.low_altitude_warning()) throw std::runtime_error{"disabling radio messages also disabled the low-altitude panel warning"};
  flight.relocate({
    .title{"Clear low-altitude warning"},
    .position{0, 0, 6000},
    .heading{0},
  });
  flight.advance({});
  if(flight.low_altitude_warning()) throw std::runtime_error{"low-altitude warning did not clear above its hysteresis range"};
  std::cout << objects << " generated placements match 32 original INITPERMS worlds" << std::endl;
  return EXIT_SUCCESS;
}

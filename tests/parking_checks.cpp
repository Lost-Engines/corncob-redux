#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include "game/parked_aircraft.h"
#include "game/scenery.h"
#include "game/session.h"
#include "resources/assets.h"
#include "tests/actor_record.h"

int main(int const argc, char const *const argv[]) {
  /// Compare original aircraft arrangement, repair, boarding and the tower's parking radius
  namespace game = corncob::game;
  namespace maths = corncob::maths;
  namespace world = corncob::world;
  if(argc != 2) throw std::invalid_argument{"parked-aircraft fixture required"};
  std::ifstream file{argv[1], std::ios::binary};
  std::vector<uint8_t> const bytes{std::istreambuf_iterator<char>{file}, {}};
  corncob::resources::binary_reader const data{bytes};
  if(bytes.size() < 16 || std::string{bytes.begin(), bytes.begin() + 8} != "CORNPAR1") throw std::invalid_argument{"invalid parked-aircraft fixture"};
  unsigned int const planes{data.dword(8)}, towers{data.dword(12)};
  unsigned int offset{16};
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset()};
  for(unsigned int i{0}; i != planes; ++i) {
    maths::world_position const base{data.signed_dword(offset), data.signed_dword(offset + 4), data.signed_dword(offset + 8)};
    maths::world_position const observer{data.signed_dword(offset + 18), data.signed_dword(offset + 22), data.signed_dword(offset + 26)};
    game::aircraft_parking parking{
      .placed_count{data.word(offset + 30)},
      .resetting{data.byte(offset + 32) != 0},
      .new_arrival{data.byte(offset + 33) != 0},
      .base_attitude{
        .heading{data.word(offset + 12)},
        .pitch{data.word(offset + 14)},
        .roll{data.word(offset + 16)},
      },
    };
    bool const on_foot{data.byte(offset + 34) != 0}, exiting{data.byte(offset + 35) != 0};
    offset += 36;
    auto object{corncob::tests::read_actor(data, offset)};
    object.aircraft_parking_flags = data.byte(offset + 65);
    object.motion.gravity = data.byte(offset + 64);
    bool const boarded{game::advance_parked_aircraft(object, base, observer, parking, on_foot, exiting)};
    if(boarded) object.flags = 0;
    offset += 74;
    auto expected{corncob::tests::read_actor(data, offset)};
    expected.motion.gravity = data.byte(offset + 64);
    corncob::tests::compare_actor(object, expected, i, "parked aircraft");
    auto const models{geometry.appearance(object)};
    if(object.aircraft_parking_flags != data.byte(offset + 65) || models.near != data.word(offset + 40)
      || models.middle != data.word(offset + 42) || models.far != data.word(offset + 44))
      throw std::runtime_error{"native parked-aircraft flags/models " + std::to_string(i)};
    offset += 74;
    if(parking.placed_count != data.word(offset) || parking.new_arrival != (data.byte(offset + 2) != 0) || boarded != (data.byte(offset + 3) != 0))
      throw std::runtime_error{"native parked-aircraft counter/boarding " + std::to_string(i)};
    offset += 4;
  }
  for(unsigned int i{0}; i != towers; ++i, offset += 30) {
    maths::world_position const tower{data.signed_dword(offset), data.signed_dword(offset + 4), data.signed_dword(offset + 8)};
    maths::world_position const observer{data.signed_dword(offset + 12), data.signed_dword(offset + 16), data.signed_dword(offset + 20)};
    bool resetting{data.byte(offset + 24) != 0};
    uint16_t placed{data.word(offset + 25)};
    auto const request{game::tower_parking_reset(tower, observer)};
    if(request != game::parking_reset::unchanged) {
      resetting = request == game::parking_reset::enable;
      if(resetting) placed = 0;
    }
    if(resetting != (data.byte(offset + 27) != 0) || placed != data.word(offset + 28))
      throw std::runtime_error{"native tower parking reset " + std::to_string(i)};
  }
  if(offset != bytes.size()) throw std::runtime_error{"parked-aircraft fixture length mismatch"};
  game::session arrival{geometry, world::decode_theatre(corncob::resources::morning_asset())};
  if(arrival.player() != game::player_mode::on_foot) throw std::runtime_error{"native arrival skipped its on-foot preparation"};
  for(unsigned int ready{0}; ready != 8 && arrival.player() == game::player_mode::on_foot; ++ready) arrival.advance({});
  if(arrival.player() != game::player_mode::in_aircraft || arrival.craft().attitude.heading != 0)
    throw std::runtime_error{"new arrival did not take its normal aircraft to the runway"};
  auto const parked{maths::world_position{arrival.craft().position}};
  arrival.press(game::action::eject);
  for(unsigned int frame{0}; frame != 8; ++frame) arrival.advance({});
  if(arrival.player() != game::player_mode::on_foot || arrival.craft().position != parked)
    throw std::runtime_error{"stepping out relocated or automatically re-entered the retained aircraft"};
  for(unsigned int frame{0}; frame != 4; ++frame) arrival.advance({
    .walk_forward{true},
  });
  if(arrival.player() != game::player_mode::on_foot) throw std::runtime_error{"returning immediately bypassed the original walk-away requirement"};
  bool walked_clear{false};
  for(unsigned int frame{0}; frame != 300; ++frame) {
    auto const distance{game::interaction_range(arrival.craft().position, arrival.pilot().position)};
    if(distance && *distance > game::reentry_departure_distance) { walked_clear = true; break; }
    arrival.advance({
      .walk_backward{true},
    });
  }
  if(!walked_clear || arrival.player() != game::player_mode::on_foot) throw std::runtime_error{"pilot could not walk clear of the retained aircraft"};
  for(unsigned int frame{0}; frame != 300 && arrival.player() == game::player_mode::on_foot; ++frame) arrival.advance({
    .walk_forward{true},
  });
  if(arrival.player() != game::player_mode::in_aircraft || !arrival.camera().cockpit || arrival.craft().position != parked)
    throw std::runtime_error{"walking away and returning failed to re-enter the same aircraft"};
  arrival.press(game::action::eject);
  for(unsigned int frame{0}; frame != 4; ++frame) arrival.advance({
    .walk_forward{true},
  });
  if(arrival.player() != game::player_mode::on_foot) throw std::runtime_error{"a later exit retained boarding permission from the previous walk-away"};
  std::cout << planes << " original parked-aircraft handlers, " << towers << " tower reset cases and exit/return boarding match" << std::endl;
  return EXIT_SUCCESS;
}

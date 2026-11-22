#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include "game/actors.h"
#include "game/portal.h"
#include "game/session.h"
#include "resources/assets.h"
#include "tests/actor_record.h"
#include "tests/reference_reader.h"

int main(int const argc, char const *const argv[]) {
  /// Verify original portal precision and PRNG call order rather than only checking a broad destination region
  if(argc != 2) throw std::invalid_argument{"portal fixture required"};
  corncob::tests::reference_reader input{argv[1], "CORNPRT2"};
  unsigned int const cases{input.dword()}, handlers{input.dword()};
  for(unsigned int i{0}; i != cases; ++i) {
    auto const flag{input.byte()};
    int32_t const start_x{input.signed_dword()}, start_y{input.signed_dword()}, start_z{input.signed_dword()};
    corncob::maths::world_position const start{start_x, start_y, start_z};
    int32_t const x{input.signed_dword()}, y{input.signed_dword()}, z{input.signed_dword()};
    corncob::maths::world_position position{x, y, z};
    corncob::maths::random_source random{input.dword()};
    int32_t const expected_x{input.signed_dword()}, expected_y{input.signed_dword()}, expected_z{input.signed_dword()};
    auto const expected_seed{input.dword()};
    corncob::game::transfer_portal(position, start, flag == 0 ? corncob::game::portal_transfer::none : flag == 255 ? corncob::game::portal_transfer::random : corncob::game::portal_transfer::home, random);
    if(position != corncob::maths::world_position{expected_x, expected_y, expected_z} || random.seed() != expected_seed) {
      throw std::runtime_error{"native portal destination or random continuation " + std::to_string(i)};
    }
  }
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset()};
  for(unsigned int i{0}; i != handlers; ++i) {
    std::array<uint8_t, 74> raw;
    for(auto &byte : raw) byte = input.byte();
    auto const initial{corncob::tests::read_actor(corncob::resources::binary_reader{raw}, 0)};
    auto const seed{input.dword()};
    for(auto &byte : raw) byte = input.byte();
    auto const expected{corncob::tests::read_actor(corncob::resources::binary_reader{raw}, 0)};
    auto const expected_seed{input.dword()};
    auto const colour{input.byte()};
    corncob::world::theatre world;
    corncob::game::combat weapons{geometry, world, seed};
    corncob::game::actors actors{geometry, world, weapons};
    auto const index{*weapons.spawn(corncob::world::object_type::portal, {0, 0, 0})};
    world.objects[index] = initial;
    corncob::maths::world_position const observer{0, 0, 0};
    corncob::maths::fixed_vector const velocity{0, 0, 0};
    actors.advance({observer, observer, velocity, false, 0}, {});
    auto const &actual{world.objects[index]};
    corncob::tests::compare_actor(actual, expected, i, "portal");
    if(actual.portal_colour != colour || weapons.random_stream().seed() != expected_seed) {
      throw std::runtime_error{"native portal timer/colour/random update " + std::to_string(i)};
    }
    geometry.prepare_colours(actual, 0, 0, 0, false);
    if(geometry.colour(0x2dd3) != colour) throw std::runtime_error{"portal colour did not reach its model primitives"};
  }
  auto portal_world{[&](unsigned int const quality){
    corncob::world::theatre world;
    world.training = true;
    world.start = corncob::maths::world_position{0, 0, 720};
    auto const &definition{geometry.definition(corncob::world::object_type::portal)};
    world.objects.push_back({
      .position{0, 0, 100'000},
      .attitude{},
      .type{corncob::world::object_type::portal},
      .flags{static_cast<uint16_t>(definition.flags | (quality << 12))},
      .timer{32'000},
    });
    return world;
  }};
  corncob::game::session transfer{geometry, portal_world(4)};
  transfer.relocate({
    .title{"Objective portal"},
    .position{0, 0, 100'000},
    .heading{0},
  });
  transfer.advance({});
  if(transfer.statistics().objectives_completed != 1 || transfer.craft().position.z > 200'000) {
    throw std::runtime_error{"portal contact did not credit its objective before the deferred transfer"};
  }
  transfer.advance({});
  if(std::abs(transfer.craft().position.z - 229'376) > 5000 || transfer.statistics().objectives_completed != 1) {
    throw std::runtime_error{"portal arrival failed or repeated its completion credit"};
  }
  corncob::game::session repaired{geometry, portal_world(6)};
  repaired.set_invincible(true);
  repaired.relocate({
    .title{"Repair portal"},
    .position{0, 0, 100'000},
    .heading{0},
  });
  repaired.advance({});
  if(!repaired.aircraft_damage().invincible || repaired.craft().position.z > 200'000) {
    throw std::runtime_error{"repair portal changed invulnerability or unexpectedly teleported the aircraft"};
  }
  auto damaged_world{portal_world(2)};
  auto const &definition{geometry.definition(corncob::world::object_type::portal)};
  damaged_world.objects.push_back({
    .position{0, 0, 229'376},
    .attitude{},
    .type{corncob::world::object_type::portal},
    .flags{static_cast<uint16_t>(definition.flags | 0x6000u)},
    .timer{32'000},
  });
  corncob::game::session damage_then_repair{geometry, std::move(damaged_world)};
  damage_then_repair.relocate({
    .title{"Damaging portal followed by repair portal"},
    .position{0, 0, 100'000},
    .heading{0},
  });
  damage_then_repair.advance({});
  auto const worst{damage_then_repair.aircraft_damage().worst};
  if(worst == 0) throw std::runtime_error{"damaging portal failed to damage the aircraft"};
  damage_then_repair.advance({});
  if(damage_then_repair.aircraft_damage().total != 0 || damage_then_repair.aircraft_damage().worst != worst) {
    throw std::runtime_error{"repair portal failed to repair current damage while preserving the sortie's historical worst"};
  }
  std::cout << cases << " original portal destinations, 256 complete portal updates and connected transfer/repair cases match" << std::endl;
  return EXIT_SUCCESS;
}

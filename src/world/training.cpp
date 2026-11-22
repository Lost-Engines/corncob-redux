#include "training.h"
#include <format>
#include <stdexcept>
#include "maths/random.h"

namespace corncob::world {

theatre generate_training(resources::geometry_bank const &geometry, training_setup const &setup, uint32_t const seed) {
  /// INITPERMS constructs the original practice airstrip, random targets/defences and fixed training obstacles
  for(auto const &defence : {setup.kla, setup.vehicles, setup.aaa}) {
    if(defence.count > 50 || defence.quality < -1 || defence.quality > 7) throw std::invalid_argument{"training defence settings are outside their range"};
  }
  theatre result;
  result.training = true;
  /// MOAG writes the generated training intelligence into airfield four's TOWERS.DAT section
  auto strength{[](training_defence const &defence){
    return defence.quality < 0 ? std::string{"random spread of strength"} : "strength " + std::to_string(defence.quality + 1);
  }};
  result.briefing = "\r\n\f\r\n\f\r\n\f\r\n\f\r\n\f\r\n";
  result.briefing += "   Your objective is to destroy the flying saucer and to take out\r\n"
    "   as many enemy fuel dumps and ground sites as possible.\r\n\r\n"
    "   Latest intelligence reports enemy strengths as follows:\r\n";
  result.briefing += std::format("             {} Missile sites:         {}\r\n", setup.kla.count, strength(setup.kla));
  result.briefing += std::format("             {} AAA sites:             {}\r\n", setup.aaa.count, strength(setup.aaa));
  result.briefing += std::format("             {} Ground-Vehicle sites:  {}\r\n", setup.vehicles.count, strength(setup.vehicles));
  result.briefing += "             1 Enemy Saucers:         random spread of strength\r\n\n\n"
    " \\(Note:\\)  Training missions are much harder than the combat missions.";
  result.start = maths::world_position{-848'968, 0, 720};
  maths::random_source random{seed};
  auto add{[&](object_type const type, int32_t const x, int32_t const y, int32_t const z, unsigned int const quality = 0)->placed_object& {
    auto const &definition{geometry.definition(type)};
    result.objects.push_back({
      .position{x, y, z},
      .attitude{},
      .velocity{maths::fixed_vector{definition.initial_velocity}},
      .type{type},
      .flags{static_cast<uint16_t>(definition.flags | 0x8000u | (quality << 12))},
      .timer{definition.initial_timer},
      .health{definition.hit_points},
      .collision_group{definition.collision_group},
      .motion{definition.motion},
      .collision_extents{maths::fixed_vector{definition.collision_extents}},
    });
    return result.objects.back();
  }};
  auto coordinate{[&]{
    int32_t const high{static_cast<int>(random.below(6)) - 3};
    return high * 65'536 + random.next();
  }};
  auto quality{[&](int const value){return value < 0 ? random.next() & 7u : static_cast<unsigned int>(value);}};
  add(object_type::runway_end, -589'825, -8000, 0);
  for(unsigned int n{1}; n <= 10; ++n) add(object_type::runway_piece, -589'825 - static_cast<int32_t>(n) * 32'768, -8000, 0);
  add(object_type::runway_end_1, -950'273, -8000, 0);
  add(object_type::runway_piece, result.start.x - 12'000, 0, 0, 7);
  for(unsigned int n{0}; n != 15; ++n) {
    int32_t const x{coordinate()};
    int32_t const y{coordinate()};
    add(object_type::fuel_dump, x, y, 1550);
  }
  for(auto const type : {object_type::aaa_site, object_type::vehicle_site, object_type::kla_site}) {
    auto const &defence{type == object_type::aaa_site ? setup.aaa : type == object_type::vehicle_site ? setup.vehicles : setup.kla};
    for(unsigned int n{0}; n != defence.count; ++n) {
      unsigned int const q{quality(defence.quality)};
      int32_t const x{coordinate()};
      int32_t const y{coordinate()};
      auto &object{add(type, x, y, 800, q)};
      object.velocity = maths::fixed_vector{0, 0, 0};
      object.timer = static_cast<uint16_t>(type == object_type::aaa_site ? 24'000 : type == object_type::vehicle_site ? 900 : 400);
      if(type == object_type::aaa_site) object.attitude.pitch = 49'152;
    }
  }
  add(object_type::forcefield_x, 32'766, 0, 16'383);
  add(object_type::forcefield_x, -32'766, 0, 16'383);
  add(object_type::forcefield_z, 0, 0, 32'766);
  if(setup.variant == training_variant::slalom) {
    result.slalom = true;
    result.briefing = "Slalom: pass six markers in order, alternating sides. Ground contact away from home resets the course. Player weapons are disabled.";
    // STRTPERM copies all nine KBUF words over the template: Z's high word is two and the placed angles are zero.
    for(auto const &position : {maths::world_position{-393'216, 98'304, 131'072}, maths::world_position{-383'216, 87'536, 131'072}, maths::world_position{-383'216, 107'536, 131'072}}) {
      add(object_type::saucer_site, position.x, position.y, position.z);
    }
    add(object_type::crater, 196'608, 163'072, 0);
    for(unsigned int n{0}; n != 50; ++n) {
      int32_t const x{coordinate()};
      int32_t const y{coordinate()};
      add(object_type::shell, x, y, 2000);
    }
    add(object_type::saucer, 0, -131'073, 1000);
    for(unsigned int row{0}; row != 2; ++row) {
      for(unsigned int marker{0}; marker != 6; ++marker) {
        add(object_type::tet, static_cast<int32_t>(marker) * 131'072, 131'072, 16'384 + static_cast<int32_t>(row) * 32'768);
      }
    }
  } else {
    add(object_type::forcefield_y, 0, 131'072, 32'766);
    add(object_type::generator_orb, 0, 0, 11'200, 1);
    add(object_type::objective_saucer, 0, 0, 163'072, quality(setup.kla.quality));
  }
  auto &plane{add(object_type::aircraft, -712'896, 8000, 720)};
  plane.attitude = {
    .heading{57'344},
    .pitch{64'036},
    .roll{0},
  };
  add(object_type::radar_base, -589'825, 16'000, 3600);
  add(object_type::radar_dish, -589'825, 16'000, 3600);
  add(object_type::ground_crew, -589'825, 22'000, 0);
  add(object_type::tower_doorway, -720'897, 14'000, 0);
  add(object_type::control_tower, -720'897, 16'000, 16'000);
  add(object_type::survey_marker, 0, 0, 0);
  add(object_type::survey_marker, 0, 0, 0);
  add(object_type::survey_marker, -2'097'152, -2'097'152, 0);
  add(object_type::survey_marker, 0, -2'097'152, 0);
  add(object_type::survey_marker, -2'097'152, 0, 0);
  return result;
}

theatre generate_slalom(resources::geometry_bank const &geometry, uint32_t const seed) {
  /// Select the preserved KA.INC layout inside INITPERMS, retaining its common placements and random sequence.
  return generate_training(geometry, {
    // The standalone source starts naas/nvest/ndbst at zero; combat defaults are supplied by MOAG instead.
    .kla{
      .count{0},
    },
    .vehicles{
      .count{0},
      .quality{-1},
    },
    .aaa{
      .count{0},
    },
    .variant{training_variant::slalom},
  }, seed);
}

} // namespace corncob::world

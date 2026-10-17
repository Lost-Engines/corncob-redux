#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include "game/session.h"
#include "game/slalom.h"
#include "graphics/scene.h"
#include "graphics/space_sequence.h"
#include "render/video_mode.h"
#include "resources/assets.h"
#include "resources/source_models.h"
#include "world/training.h"

namespace {
void require(bool const condition, char const *const message) {
  if(!condition) throw std::runtime_error{message};
}
}

auto main()->int {
  /// Exercise the recovered course through its session and renderer, independently of window/input callbacks
  namespace game = corncob::game;
  namespace world = corncob::world;
  namespace render = corncob::render;
  namespace resources = corncob::resources;
  game::slalom_course course{-1};
  for(unsigned int i{0}; i != game::slalom_course::marker_count; ++i) {
    auto const complete{course.advance({static_cast<int32_t>(i) * game::slalom_course::marker_spacing + 1,
      game::slalom_course::centre_y + ((i & 1u) != 0 ? -65'536 : 65'536), 20'000}, false, false)};
    require(course.progress() == i + 1 && complete == (i == 5), "ordered slalom crossing failed");
  }
  require(!course.advance({700'000, 0, 20'000}, false, false), "course awarded completion twice");
  game::slalom_course wrong_side{-1};
  wrong_side.advance({1, 196'608, 20'000}, false, false);
  wrong_side.advance({131'073, 196'608, 20'000}, false, false);
  require(wrong_side.progress() == 0, "wrong-side crossing accepted");
  game::slalom_course skipped{-1};
  skipped.advance({1, 196'608, 20'000}, false, false);
  skipped.advance({262'145, 196'608, 20'000}, false, false);
  require(skipped.progress() == 0, "skipped checkpoint accepted");
  game::slalom_course landing{-1};
  landing.advance({1, 196'608, 20'000}, false, false);
  landing.advance({2, 196'608, 720}, true, true);
  require(landing.progress() == 1, "home landing reset slalom");
  landing.advance({3, 196'608, 720}, true, false);
  require(landing.progress() == 0, "away landing failed to reset slalom");
  resources::geometry_bank geometry{resources::engine_data_asset(), true};
  require(geometry.definition(world::object_type::saucer).models.near == resources::source_bmrobj, "farm model not selected");
  require(geometry.definition(world::object_type::shell).collision_extents.z == 8000, "tree collision dimensions lost");
  auto const original{world::generate_training(geometry, {})};
  auto const recovered{world::generate_slalom(geometry)};
  auto count{[&](world::object_type const type){ return std::ranges::count(recovered.objects, type, &world::placed_object::type); }};
  require(recovered.slalom && recovered.training && count(world::object_type::tet) == 12 && count(world::object_type::shell) == 50
    && count(world::object_type::saucer) == 1 && count(world::object_type::saucer_site) == 3, "source layout omitted objects");
  require(count(world::object_type::objective_saucer) == 0 && count(world::object_type::generator_orb) == 0, "combat-only generator branch leaked into source layout");
  require(count(world::object_type::kla_site) == 0 && count(world::object_type::vehicle_site) == 0 && count(world::object_type::aaa_site) == 0,
    "standalone slalom inherited the combat training defence counts");
  require(count(world::object_type::forcefield_x) == 2 && count(world::object_type::forcefield_z) == 1,
    "source slalom barriers were removed with the combat training sites");
  unsigned int npc{0};
  std::array<corncob::maths::world_position, 3> const source_planes{
    corncob::maths::world_position{-393'216, 98'304, 131'072},
    corncob::maths::world_position{-383'216, 87'536, 131'072},
    corncob::maths::world_position{-383'216, 107'536, 131'072},
  };
  for(auto const &object : recovered.objects) if(object.type == world::object_type::saucer_site) {
    require(object.position == source_planes[npc++] && object.attitude.heading == 0 && object.attitude.pitch == 0 && object.attitude.roll == 0,
      "KBUF did not override the NPC template's position and angles");
  }
  require(original.objects.front().position == recovered.objects.front().position, "common airfield changed");
  world::theatre damage_world;
  damage_world.slalom = true;
  for(auto const type : {world::object_type::saucer, world::object_type::shell, world::object_type::saucer_site}) {
    damage_world.objects.push_back(*std::ranges::find(recovered.objects, type, &world::placed_object::type));
  }
  game::combat damage{geometry, damage_world};
  world::placed_object const bullet{
    .position{},
    .attitude{},
    .type{world::object_type::bullets},
    .flags{std::to_underlying(world::object_flag::present)},
  };
  unsigned int const barn_hits{damage_world.objects[0].health};
  damage.damage(0, bullet);
  require(damage_world.objects[0].attitude.roll == 0 && damage_world.objects[0].health == barn_hits - 1,
    "slalom barn inherited a saucer's reaction to damage");
  for(unsigned int hit{1}; hit != barn_hits; ++hit) damage.damage(0, bullet);
  require(damage_world.objects[0].type == world::object_type::destroyed_fuel_dump
    && damage_world.objects[0].pose == world::model_pose::fuel_wreck && damage_world.objects[0].motion.spin == corncob::maths::fixed_vector{0, 0, 0},
    "slalom barn did not use the source's fuel-dump destruction handler");
  auto const tree_flags{damage_world.objects[1].flags};
  damage.damage(1, bullet);
  require(damage_world.objects[1].flags == tree_flags && damage_world.objects[1].health == 1,
    "slalom tree ran the shell's hidden-enemy collision handler");
  auto const plane_timer{damage_world.objects[2].timer};
  damage.damage(2, bullet);
  require(damage_world.objects[2].health == 1 && damage_world.objects[2].timer == plane_timer,
    "slalom traffic aircraft ran the saucer launch-site damage handler");
  game::session flight{geometry, recovered};
  flight.set_invincible(true);
  flight.press(game::action::drop_bomb);
  for(unsigned int i{0}; i != 30; ++i) flight.advance({
    .weapons{
      .gun{true},
      .cannon{true},
      .missile{true},
    },
  });
  require(flight.sounds().gunshots == 0 && flight.sounds().missiles == 0 && flight.bomb_supply() == 3, "guns=0 left a weapon enabled");
  for(unsigned int i{0}; i != game::slalom_course::marker_count; ++i) {
    flight.relocate({
      .title{"Course integration"},
      .position{static_cast<int32_t>(i) * game::slalom_course::marker_spacing + 1,
        game::slalom_course::centre_y + ((i & 1u) != 0 ? -65'536 : 65'536), 20'000},
      .heading{0},
    });
    flight.advance({});
  }
  require(flight.slalom_progress() == 6 && flight.statistics().objectives_completed == 1, "session failed to award the completed course");
  require(flight.take_radio() == resources::radio_notice::objective_complete, "slalom completion lost its original mission-objective message");
  auto const cockpit{resources::decode_cockpit_mask(resources::cockpit_asset())};
  for(auto const video : {render::video_mode::ega, render::video_mode::ega_low, render::video_mode::hercules}) {
    corncob::graphics::scene_renderer renderer{geometry, cockpit, video};
    render::framebuffer output;
    auto const size{render::dimensions(video)};
    corncob::graphics::instrument_telemetry const instruments{
      .aircraft{flight.craft()},
      .controls{},
      .slalom{true},
      .slalom_progress{3},
    };
    for(auto const pitch : {uint16_t{0}, uint16_t{16'384}, uint16_t{49'152}}) {
      auto observer{flight.camera()};
      observer.attitude.pitch = pitch;
      renderer.preview(flight.world(), observer, 0, 0, flight.random_stream(), output, &instruments);
      require(output.width == size.width && output.height == size.height && output.pixels.size() == size.width * size.height, "video dimensions incorrect");
      if(video == render::video_mode::hercules) require(std::ranges::all_of(output.pixels, [](auto const &pixel){
        return pixel.red == pixel.green && pixel.red == pixel.blue && (pixel.red == 0 || pixel.red == 255);
      }), "Hercules emitted non-monochrome pixels");
      corncob::graphics::space_sequence ending{renderer.indexed_frame(), renderer.colours()};
      ending.advance(1.0);
      ending.render(output);
      require(output.width == 640 && output.height == 350 && output.pixels.size() == 640 * 350, "ending inherited an incompatible flight surface");
      observer.cockpit = false;
      renderer.preview(flight.world(), observer, 0, 0, flight.random_stream(), output);
    }
  }
  // Raw mask contents in the main window must never leak into the adapted frame or its corners.
  auto marked_mask{cockpit};
  std::fill_n(marked_mask.begin(), 199 * render::screen_layout::mask_stride, uint8_t{0xff});
  world::theatre empty_world;
  corncob::graphics::camera const observer{
    .position{0, 0, 720},
    .attitude{},
    .cockpit{true},
    .rear_view{false},
    .ground_detail{false},
  };
  for(auto const video : {render::video_mode::ega_low, render::video_mode::hercules}) {
    corncob::graphics::scene_renderer normal{geometry, cockpit, video}, marked{geometry, marked_mask, video};
    render::framebuffer expected, actual;
    normal.preview(empty_world, observer, 0, 0, flight.random_stream(), expected);
    marked.preview(empty_world, observer, 0, 0, flight.random_stream(), actual);
    require(std::ranges::equal(expected.pixels, actual.pixels, [](auto const &a, auto const &b){
      return a.red == b.red && a.green == b.green && a.blue == b.blue;
    }), "raw cockpit viewport marks leaked into the adapted display");
  }
  corncob::graphics::scene_renderer standard{geometry, cockpit};
  render::framebuffer standard_frame;
  auto random{flight.random_stream()};
  standard.render(empty_world, observer, 0, 0, random, standard_frame);
  auto const &indices{standard.indexed_frame()};
  require(indices.width == 640 && indices.height == 350, "Redux viewport changed native source dimensions");
  auto const low_crt{render::fit_viewport(960, 720, 320, 200, render::display_aspect::crt)};
  require(low_crt.width == 960 && low_crt.height == 720, "low-resolution CRT scaling lost its requested integer factor");
  require(render::hercules_colour(2, 1) == 15 && render::hercules_colour(2, 0) == 0, "Hercules ground mask changed");
  std::cout << "Source course ordering, resets, bundled geometry, weapon suppression and all graphics profiles passed" << std::endl;
  return EXIT_SUCCESS;
}

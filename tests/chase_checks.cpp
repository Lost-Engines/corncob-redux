#include <algorithm>
#include <iostream>
#include <stdexcept>
#include "game/preferences.h"
#include "game/session.h"
#include "resources/assets.h"
#include "world/theatre_catalog.h"

namespace {
namespace g = corncob::game;
namespace w = corncob::world;
namespace m = corncob::maths;
namespace gr = corncob::graphics;
namespace r = corncob::resources;

void require(bool value, char const *message) {
  if(!value) throw std::runtime_error{message};
}

void equivalent(g::session &a, g::session &b) {
  require(w::encode_theatre(a.snapshot()) == w::encode_theatre(b.snapshot()), "camera choice changed the saved world");
  require(a.player() == b.player() && a.pilot().position == b.pilot().position && a.craft().position == b.craft().position
    && a.craft().velocity == b.craft().velocity, "camera choice changed player motion");
  require(a.random_stream().seed() == b.random_stream().seed() && a.timer() == b.timer()
    && a.results().encode() == b.results().encode(), "camera choice changed simulation timing, randomness or mission results");
}

void simulation_checks(r::geometry_bank const &geometry) {
  // Include live actors, weapon fire and repeated view changes: the new display
  // position must not feed back into any of the original proximity mechanisms.
  auto theatre{w::load_theatre("morning")};
  g::session first{geometry, theatre, true}, third{geometry, theatre, true};
  first.set_invincible(true); third.set_invincible(true);
  third.press(g::action::third_person);
  auto const original{first.camera()};
  auto const chase{third.render_camera()};
  require(chase.external && chase.subject && chase.subject->type == w::object_type::aircraft, "F7 did not show the occupied aircraft");
  require(!chase.cockpit && !chase.rear_view && chase.position != original.position, "F7 retained a cockpit or failed to offset the camera");
  require(third.camera().position == original.position, "chase view moved the logical observer");
  require(chase.subject->attitude.heading == first.craft().attitude.heading && chase.subject->attitude.pitch == first.craft().attitude.pitch, "chase aircraft lost its parked attitude");
  require(chase.position.z >= 200, "parked chase camera went underground");
  auto const world_before{w::encode_theatre(third.world())};
  for(unsigned int i{0}; i < 10; ++i) (void)third.render_camera();
  require(w::encode_theatre(third.world()) == world_before, "render camera inserted its player model into the simulation");
  third.press(g::action::third_person);
  require(third.render_camera().external, "F7 toggled the view off instead of selecting it");
  for(unsigned int i{0}; i < 240; ++i) {
    g::input_state input{.throttle_up{i < 20}, .weapons{.gun{i % 4 == 0}}};
    if(i > 120) input.stick.roll = 200;
    first.advance_time(input, 1.0 / 60); third.advance_time(input, 1.0 / 60);
    (void)third.render_camera();
    if(i % 19 == 0) { third.press(g::action::first_person); third.press(g::action::third_person); }
  }
  equivalent(first, third);
  first.press(g::action::eject); third.press(g::action::eject);
  require(third.render_camera().subject && third.render_camera().subject->type == w::object_type::ground_crew, "ejection did not switch the chase subject to the pilot");
  for(unsigned int i{0}; i < 120; ++i) {
    g::input_state const input{.walk_forward{true}, .look_yaw{20}, .weapons{.gun{i % 9 == 0}}};
    first.advance_time(input, 1.0 / 60); third.advance_time(input, 1.0 / 60);
    (void)third.render_camera();
  }
  equivalent(first, third);
  third.press(g::action::toggle_remote);
  require(third.render_camera().subject->type == w::object_type::ground_crew, "remote controls moved the chase view away from the person");
  third.press(g::action::toggle_map);
  require(third.render_camera().map_mode && !third.render_camera().subject, "chase view replaced the map");
  third.press(g::action::toggle_map);
  require(third.render_camera().external, "closing the map forgot the chase view");
  third.press(g::action::first_person);
  require(!third.render_camera().external && !third.render_camera().subject && !third.render_camera().cockpit, "F6 did not restore first person on foot");
  g::session cockpit{geometry, theatre, true};
  cockpit.press(g::action::toggle_cockpit); cockpit.press(g::action::third_person); cockpit.press(g::action::first_person);
  require(cockpit.render_camera().cockpit && !cockpit.render_camera().external, "F6 did not restore the normal cockpit after L/F7");
  cockpit.begin_builder(); cockpit.press(g::action::third_person);
  require(!cockpit.render_camera().external && !cockpit.render_camera().subject, "F7 interfered with the mission builder");
}

void boarding_checks(r::geometry_bank const &geometry) {
  for(uint16_t const variant : {uint16_t{0}, uint16_t{0x1000}, uint16_t{0x2000}}) {
    w::theatre theatre;
    theatre.start.z = 720;
    theatre.objects.push_back({.position{0,-3000,720}, .attitude{}, .type{w::object_type::aircraft}, .flags{static_cast<uint16_t>(1u | variant)}});
    g::session game{geometry, theatre};
    game.press(g::action::third_person);
    game.advance({});
    require(game.player() == g::player_mode::in_aircraft, "chase camera prevented boarding a nearby aircraft");
    require(game.render_camera().subject && (game.render_camera().subject->flags & 0x3000u) == variant,
      "external aircraft lost its standard, reserve or experimental variant");
    game.press(g::action::eject);
    require(game.render_camera().subject->type == w::object_type::ground_crew, "grounded exit did not retain third person");
    game.advance_time({.walk_backward{true}}, 2);
    game.advance_time({.walk_forward{true}}, 3);
    require(game.player() == g::player_mode::in_aircraft && game.render_camera().external
      && game.render_camera().subject->type == w::object_type::aircraft, "walking back into the aircraft failed or forgot third person");
  }
}

void rendering_checks(r::geometry_bank &geometry) {
  auto const cockpit{r::decode_cockpit_mask(r::cockpit_asset())};
  gr::scene_renderer renderer{geometry, cockpit};
  renderer.configure({.resolution{800,600}, .ground_points{0}});
  corncob::render::framebuffer output;
  m::random_source random;
  w::theatre theatre;
  theatre.start = {0,0,720};
  g::session pedestrian{geometry, theatre};
  pedestrian.press(g::action::third_person);
  auto view{pedestrian.render_camera()};
  view.gunsight = false;
  auto const stats{renderer.render(pedestrian.world(), view, 0, 0, random, output)};
  require(stats.objects == 1, "third-person pilot was not rendered");
  auto const &frame{renderer.indexed_frame()};
  auto const red{std::ranges::count(frame.pixels, uint8_t{12})};
  require(red > 500, "player shirt was not red or the pilot was off screen");
  auto const player_frame{frame.pixels};
  view.subject.reset(); view.external = false;
  theatre.objects.push_back(*pedestrian.render_camera().subject);
  renderer.render(theatre, view, 0, 0, random, output);
  require(std::ranges::count(frame.pixels, uint8_t{12}) == 0 && std::ranges::count(frame.pixels, uint8_t{6}) > 500,
    "player styling leaked into ordinary ground crew");
  require(player_frame != frame.pixels, "player styling was not distinct from ground crew");
  // Pure overhead / ground-looking poses must remain finite and above ground.
  pedestrian.advance_time({.look_pitch{16000}}, 1.0 / 60);
  require(pedestrian.render_camera().position.z >= 200, "looking down put the chase camera underground");
  pedestrian.advance_time({.look_pitch{-32000}}, 1.0 / 60);
  require(pedestrian.render_camera().position.z >= 200, "looking up put the chase camera underground");
  renderer.render(pedestrian.world(), pedestrian.render_camera(), 0, 0, random, output);
  // A wall must occlude the visual-only subject through the same depth buffer.
  auto hidden{pedestrian.render_camera()};
  hidden.position = {-10000,0,1000}; hidden.attitude = {};
  hidden.subject->position = {0,0,0}; hidden.subject->attitude = {};
  theatre.objects.clear();
  theatre.objects.push_back({.position{-5000,0,16000}, .attitude{}, .type{w::object_type::control_tower}, .flags{1}});
  renderer.render(theatre, hidden, 0, 0, random, output);
  require(std::ranges::count(frame.pixels, uint8_t{12}) == 0, "player model drew through a foreground tower");
}
}

int main() {
  r::geometry_bank geometry{r::engine_data_asset()};
  simulation_checks(geometry);
  boarding_checks(geometry);
  rendering_checks(geometry);
  auto const bindings{g::default_bindings()};
  for(auto const &[id,key] : {std::pair{g::control_id::first_person,295}, {g::control_id::third_person,296}}) {
    auto const found{std::ranges::find(bindings, id, &g::control_binding::id)};
    require(found != bindings.end() && found->key == key && found->context == g::control_context::both, "F6/F7 defaults do not work in both contexts");
  }
  require(std::ranges::none_of(bindings, [](auto const &b){ return b.matches_key(297); }), "F8 was not left free");
  std::cout << "Third-person cameras, player visuals, isolation and bindings passed\n";
}

#pragma once

#include <functional>
#include <optional>
#include <string>
#include "flight/damage.h"
#include "flight/continuous.h"
#include "flight/landing.h"
#include "game/actors.h"
#include "game/combat.h"
#include "game/map_view.h"
#include "game/mission_builder.h"
#include "game/pilot_motion.h"
#include "game/portal.h"
#include "game/recorded_flight.h"
#include "game/rocket.h"
#include "game/slalom.h"
#include "game/sortie_clock.h"
#include "game/vision.h"
#include "game/timing.h"
#include "graphics/scene.h"
#include "resources/geometry_bank.h"
#include "resources/radio_messages.h"
#include "world/world.h"

namespace corncob::game {

enum class player_mode {
  on_foot,
  in_aircraft,
  dead,
  captured,
  returned,
  rescued,
  earth_saved,
};

enum class action {
  throttle_up,
  throttle_down,
  toggle_flaps,
  toggle_autopilot,
  rocket,
  eject,
  parachute,
  toggle_remote,
  toggle_rear,
  toggle_cockpit,
  toggle_ground,
  toggle_look,
  toggle_map,
  call_rescue,
  toggle_messages,
  pause,
  drop_bomb,
  plant_charge,
  arm_charges,
  toggle_invulnerability,
  first_person,
  third_person,
};

struct view_input {
  bool left{false};
  bool right{false};
  bool up{false};
  bool down{false};
  bool below{false};
  bool modifier{false};
};

struct input_state {
  flight::control_input stick{};
  bool joystick_controlled{false};
  bool throttle_up{false};
  bool throttle_down{false};
  bool walk_forward{false};
  bool walk_backward{false};
  bool accelerated_walking{false};
  bool jump{false};
  bool drop_through{false};
  int16_t strafe{0};
  int16_t look_yaw{0};
  int16_t look_pitch{0};
  weapon_controls weapons{};
  view_input view{};
  map_input map{};
};


class session {
#ifndef NDEBUG
  friend struct capture_access; // Optional developer capture tool; absent from release builds.
#endif
private:
  resources::geometry_bank const &geometry;
  world::theatre theatre;
  combat weapons;
  actors enemies;
  proximity_sound audible_proximity;
  flight::aircraft_state aircraft;
  flight::damage_state damage;
  maths::random_source &random;
  resources::particle_animation particles;
  uint32_t last_wall_damage{0};
  std::vector<unsigned int> aircraft_contacts;
  uint16_t sortie_flags{0};
  uint16_t aircraft_losses{0};
  world::object_type fatal_contact{world::object_type::count};
  bool airframe_destroyed{false};
  bool left_ground{false};
  flight::angular_response angular;
  flight::landing_state landing;
  aircraft_parking parking;
  flight::atmosphere atmosphere;
  person_state person;
  flight::continuous_state continuous;
  maths::motion_fraction person_fraction;
  vec2<double> tumble_fraction{0,0}, head_fraction{0,0};
  double elapsed_phase{0}, pit_fraction{0}, capture_fraction{0};
  timing::frame_clock host_clock;
  vision_state vision;
  std::optional<unsigned int> controlled_aircraft;
  player_mode mode{player_mode::on_foot};
  bool reentry_allowed{false};
  bool remote{false};
  bool rear{true};
  bool cockpit{true};
  bool chase_view{false};
  bool ground_detail{true};
  bool paused{false};
  bool look_mode{false};
  bool looking{false};
  bool walking{false};
  maths::angles head_angles;
  std::optional<map_view> map;
  std::optional<mission_builder> builder;
  std::optional<slalom_course> course;
  std::optional<recorded_flight> recording;
  unsigned int recorded_index{0}, recorded_ticks{0};
  bool recording_complete{false};
  bool experimental_clearance{false};
  bool beginner_flight{false};
  bool invincible{false};
  uint16_t orb_distortion{0};
  vec3<uint16_t> palette_phase{0, 0, 0};
  uint32_t fast_ticks{0};
  sortie_clock clock;
  rocket_charge rocket_timer;
  portal_transfer pending_portal{portal_transfer::none};
  bool rocket_requested{false};
  uint16_t frame{0};
  std::string notice;
  bool messages{true};
  bool exit_warned{false};
  bool tower_request{false};
  bool visiting_tower{false};
  bool low_altitude{false};
  bool steering_warned{false};
  std::optional<resources::radio_notice> radio;

  void advance_step(input_state const &input, flight::time_step step, std::optional<std::function_ref<void()>> draw_frame);
  void update_person_elapsed(input_state const &input, flight::time_step step);
  void update_aircraft(input_state const &input, flight::time_step step);
  void update_scenery();
  void update_person(input_state const &input, flight::time_step step);
  void find_surfaces();
  void check_aircraft_entry();
  void check_aircraft_collisions(flight::time_step step);
  void enter_portal(world::placed_object &portal, flight::time_step step);
  bool at_home(uint16_t maximum_distance = 4200) const;
  void enter_aircraft(unsigned int index);
  void leave_aircraft();
  void retain_aircraft();

public:
  session(resources::geometry_bank const &geometry, world::theatre theatre, bool reserve_aircraft = false);
  void press(action command);
  session(session const &) = delete;
  session &operator=(session const &) = delete;
  session(session &&) = delete;
  session &operator=(session &&) = delete;
  void advance(input_state const &input, std::optional<std::function_ref<void()>> draw_frame = std::nullopt);
  void advance_time(input_state const &input, double seconds);
  double update_frame(double timestamp, input_state const &input, bool accelerated = false);
  maths::random_source &random_stream() noexcept;
  graphics::camera camera() const; // Logical observer used by gameplay.
  graphics::camera render_camera() const; // Presentation only; may follow behind the player.
  world::theatre const &world() const noexcept;
  player_mode player() const noexcept;
  flight::aircraft_state const &craft() const noexcept;
  person_state const &pilot() const noexcept;
  std::string const &message() const noexcept;
  uint32_t timer() const noexcept;
  void suspend_clock() noexcept;
  unsigned int frame_number() const noexcept;
  bool is_paused() const noexcept;
  unsigned int slalom_progress() const noexcept {
    return course ? course->progress() : 0;
  }
  bool autopilot_engaged() const noexcept;
  bool looking_around() const noexcept;
  bool in_map() const noexcept;
  bool in_builder() const noexcept;
  bool in_recording() const noexcept;
  void begin_builder();
  void begin_recording(std::span<uint8_t const> data);
  void edit(builder_command command);
  void builder_speed(unsigned int setting);
  bool messages_enabled() const noexcept;
  std::optional<resources::radio_notice> take_radio();
  bool request_exit();
  bool take_tower();
  void resume_from_tower();
  flight::damage_state const &aircraft_damage() const noexcept;
  sortie_statistics const &statistics() const noexcept;
  sound_cues const &sounds() const noexcept;
  proximity_sound const &proximity_sounds() const noexcept;
  bool remote_control() const noexcept;
  bool low_altitude_warning() const noexcept;
  unsigned int bomb_supply() const noexcept;
  uint8_t rocket_indicator_colour() const noexcept;
  void relocate(world::mission_start const &start);
  void set_invincible(bool value) noexcept;
  void set_beginner_flight(bool enabled) noexcept;
  void set_experimental_clearance(bool permitted) noexcept;
  sortie_statistics results(bool aborted = false) const;
  world::theatre snapshot() const;
  bool finished() const noexcept;
};

} // namespace corncob::game

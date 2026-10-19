#include "session.h"
#include <algorithm>
#include <cmath>
#include "game/timing.h"

namespace corncob::game {

double session::update_frame(double const timestamp, input_state const &requested, bool const accelerated) {
  bool const fast{accelerated && !map && !builder && !paused};
  auto input{requested};
  input.accelerated_walking = fast && mode == player_mode::on_foot && !remote;
  double const seconds{host_clock.advance(timestamp, !paused && !visiting_tower && !finished(), fast)};
  advance_time(input, seconds);
  return seconds;
}

void session::advance_time(input_state const &requested, double seconds) {
  if(!std::isfinite(seconds) || seconds <= 0 || paused || visiting_tower || finished()) return;
  input_state input{requested};
  while(seconds > 1e-12 && !paused && !visiting_tower && !finished()) {
    double const duration{std::min(seconds, timing::maximum_step)};
    elapsed_phase += duration;
    bool const tick{elapsed_phase + 1e-12 >= timing::seconds_per_step};
    if(tick) elapsed_phase -= timing::seconds_per_step;
    advance_step(input, {.seconds{duration}, .clock_tick{tick}}, std::nullopt);
    input.look_yaw = input.look_pitch = 0;
    seconds -= duration;
  }
}

void session::update_person_elapsed(input_state const &input, flight::time_step const step) {
  double const ratio{step.seconds/timing::seconds_per_step};
  if(!remote) {
    double const retained{maths::decay(9500.0/10100.0, ratio)};
    for(unsigned int axis{0}; axis < 2; ++axis) {
      double const value{(person.tumble[axis]+tumble_fraction[axis])*retained};
      person.tumble[axis] = static_cast<int16_t>(std::trunc(value));
      tumble_fraction[axis] = value-person.tumble[axis];
    }
    maths::turn(person.attitude.heading, -input.stick.roll*ratio+input.look_yaw, person_fraction.angles.x);
    maths::turn(person.attitude.pitch, (-input.stick.pitch+person.tumble.y)*ratio+input.look_pitch, person_fraction.angles.y);
    person.attitude.pitch = static_cast<maths::angle>(std::clamp<int>(maths::word(person.attitude.pitch), -16000, 16000));
    maths::turn(person.attitude.roll, -maths::word(person.attitude.roll)*(1-maths::decay(63.0/64.0,ratio)), person_fraction.angles.z);
    if(person.swing_frames > 0) {
      if(std::abs(input.stick.roll)+std::abs(input.stick.pitch) > 32) person.swing_frames = 0;
      else if(person.swing_frames <= 500) {
        if(person.swing_frames == 500 && step.clock_tick) person.attitude = person.ejection_attitude;
        int const envelope{std::max(0, static_cast<int>(person.swing_frames)-250)};
        double const swing{((envelope*maths::cosine(static_cast<maths::angle>(envelope*900))) >> 16)*4.0};
        maths::turn(person.attitude.pitch, swing*ratio - maths::word(person.attitude.pitch)*(1-maths::decay(31.0/32.0,ratio)), person_fraction.angles.y);
      }
      if(step.clock_tick && person.swing_frames > 0) --person.swing_frames;
    }
  }
  int const forward{static_cast<int>(input.walk_forward)-static_cast<int>(input.walk_backward)};
  int const sideways{input.strafe};
  if(forward != 0 || sideways != 0) {
    double const normalise{1.0/std::hypot(static_cast<double>(forward), static_cast<double>(sideways))};
    double const stride{(input.accelerated_walking ? 249.5 : 99.0)*ratio*normalise};
    auto const rotation{maths::rotation_real({person.attitude.heading+person_fraction.angles.x,0,0})};
    maths::move(person.position, rotation.apply({forward*stride,sideways*stride,0}), person_fraction.position);
  }
  if(input.jump && person.standing && !person.charging_jump) {
    person.charging_jump = true; person.velocity.z = 4000; person_fraction.velocity.z = 0;
    person.standing = false; person.parachute = false; person.horizontal_damping = 31000;
  }
  if(!input.jump) person.charging_jump = false;
  if(input.drop_through) person.surface_height = 0;
  int32_t const floor{person.surface_height+flight::landing_state::aircraft_height};
  if(person.position.z <= floor && person.velocity.z <= 0) {
    person.position.z = floor; person_fraction.position.z = 0;
    person.velocity.z = 0; person_fraction.velocity.z = 0;
    person.swing_frames = 0; person.standing = true; return;
  }
  person.standing = false;
  if(person.parachute) person.horizontal_damping = 30000;
  auto const before{vec3<double>{person.velocity}+person_fraction.velocity};
  vec3<double> after{before}, distance{0,0,0};
  double const terminal{person.parachute ? 900.0 : 4800.0};
  for(unsigned int axis{0}; axis < 3; ++axis) {
    double const retained{axis == 2 ? 1-50.0/terminal : person.horizontal_damping/32768.0};
    double const equilibrium{axis == 2 ? 50-terminal : 0};
    double const attenuation{maths::decay(retained,ratio)};
    after[axis] = equilibrium+(before[axis]-equilibrium)*attenuation;
    distance[axis] = (equilibrium*ratio+(before[axis]-equilibrium)*(attenuation-1)/std::log(retained))/16.0;
  }
  maths::move(person.position, distance, person_fraction.position);
  for(unsigned int axis{0}; axis < 3; ++axis) {
    person.velocity[axis] = static_cast<int16_t>(std::trunc(after[axis]));
    person_fraction.velocity[axis] = after[axis]-person.velocity[axis];
  }
  if(person.position.z < floor) {
    int const descent{-static_cast<int>(person.velocity.z)};
    if(descent >= 1800 && (person.parachute || descent >= 3200) && !invincible) {
      mode = player_mode::dead; notice = "Fatal fall";
    } else {
      person.position.z = floor; person.velocity.z = 0; person.standing = true;
      person_fraction.position.z = person_fraction.velocity.z = 0;
    }
  }
}

} // namespace corncob::game

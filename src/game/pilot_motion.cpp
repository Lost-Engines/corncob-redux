#include "pilot_motion.h"
#include <algorithm>
#include <cstdlib>
#include "maths/integer.h"

namespace corncob::game {

maths::world_position walking_displacement(maths::angle const heading, maths::fixed_vector const &stride, flight::time_step const step) {
  /// GOODSTEP applies WTADJ before rotation, then MOVEOBS applies DWTADJ after its arithmetic shift
  maths::fixed_vector adjusted{0, 0, 0};
  for(unsigned int axis{0}; axis < 2; ++axis) adjusted[axis] = maths::word(static_cast<int32_t>(stride[axis]) * step.elapsed / step.normal);
  auto const motion{maths::transform(maths::make_inverse_rotation({.heading{heading}}), adjusted)};
  maths::world_position result{0, 0, 0};
  for(unsigned int axis{0}; axis < 2; ++axis)
    result[axis] = maths::rounded_divide(maths::scale(motion[axis] >> 4, step.elapsed), step.normal);
  return result;
}
void damp_fall_velocity(maths::fixed_vector &velocity, int16_t const horizontal_damping, int16_t const terminal_velocity, flight::time_step const step) {
  /// DAMPCHUTE doubles the high product word, preserving its even-only horizontal velocities and time-adjusted vertical drag
  for(unsigned int axis{0}; axis != 2; ++axis) velocity[axis] = maths::word((static_cast<int32_t>(velocity[axis]) * horizontal_damping >> 16) * 2);
  int32_t const drag{maths::rounded_divide(maths::scale(static_cast<int32_t>(velocity.z) * 50, step.elapsed), step.normal)};
  if(std::abs(static_cast<int>(maths::word(drag >> 16))) >= std::abs(static_cast<int>(terminal_velocity))) { velocity.z = 0; return; }
  velocity.z = maths::word(velocity.z - drag / terminal_velocity);
}
void swing_parachute(person_state &person, flight::control_input const &input) {
  /// SWING waits eight frames, restores the recorded ejection attitude, then lets its cosine envelope settle toward level until the pilot intervenes
  if(person.swing_frames == 0) return;
  if(std::abs(static_cast<int>(input.roll)) + std::abs(static_cast<int>(input.pitch)) > 32) { person.swing_frames = 0; return; }
  unsigned int constexpr swing_duration{500};
  if(person.swing_frames == swing_duration) {
    person.tumble = vec2<int16_t>{0, 0};
    person.attitude = person.ejection_attitude;
  } else if(person.swing_frames < swing_duration) {
    int const envelope{std::max(0, static_cast<int>(person.swing_frames) - static_cast<int>(swing_duration / 2))};
    auto const phase{static_cast<maths::angle>(envelope * 900)};
    int16_t const swing{maths::word(((envelope * maths::cosine(phase)) >> 16) * 4)};
    person.attitude.pitch += static_cast<maths::angle>(swing);
    person.attitude.pitch += static_cast<maths::angle>(maths::negate(maths::word(person.attitude.pitch)) >> 5);
  }
  --person.swing_frames;
}

} // namespace corncob::game

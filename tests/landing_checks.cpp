#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include "flight/landing.h"
#include "tests/reference_reader.h"

namespace {

template<typename value_type>
void check(value_type const actual, value_type const expected, char const *const field, unsigned int const index) {
  /// Identify the first differing native suspension or contact result
  if(actual != expected) {
    throw std::runtime_error{std::string{field} + " case " + std::to_string(index)
      + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual)};
  }
}
corncob::maths::angles angles(corncob::tests::reference_reader &input) {
  /// Read an original attitude independent of native structure packing
  return {
    .heading{input.word()},
    .pitch{input.word()},
    .roll{input.word()},
  };
}

} // anonymous namespace

int main(int const argc, char const *const argv[]) {
  /// Compare wheel geometry and actual 3.42 suspension with original machine code
  if(argc != 3) throw std::invalid_argument{"landing_checks requires landing and recovery fixture paths"};
  corncob::tests::reference_reader input{argv[1], "CORNLD01"};
  auto const magnitudes{input.dword()};
  auto const wheels{input.dword()};
  auto const contacts{input.dword()};
  for(unsigned int i{0}; i != magnitudes; ++i) {
    corncob::maths::fixed_vector const value{input.signed_word(), input.signed_word(), input.signed_word()};
    check(corncob::maths::magnitude(value), input.word(), "velocity magnitude", i);
  }
  for(unsigned int i{0}; i != wheels; ++i) {
    corncob::flight::aircraft_state aircraft;
    aircraft.attitude = angles(input);
    aircraft.body_to_world = corncob::maths::make_inverse_rotation(aircraft.attitude);
    corncob::flight::landing_state landing;
    bool const expected{input.byte() != 0};
    check(corncob::flight::find_wheel_height(aircraft, landing), expected, "wheel envelope", i);
    if(expected) {
      check(landing.wheel_heights.x, input.signed_word(), "left wheel", i);
      check(landing.wheel_heights.y, input.signed_word(), "right wheel", i);
      check(landing.clearance, input.signed_word(), "wheel clearance", i);
    }
  }
  for(unsigned int i{0}; i != contacts; ++i) {
    corncob::flight::aircraft_state aircraft;
    aircraft.attitude = angles(input);
    aircraft.body_to_world = corncob::maths::make_inverse_rotation(aircraft.attitude);
    corncob::flight::landing_state landing;
    landing.stopped = false;
    if(!corncob::flight::find_wheel_height(aircraft, landing)) throw std::runtime_error{"invalid native ground fixture attitude"};
    for(unsigned int axis{0}; axis != 3; ++axis) aircraft.body_velocity[axis] = input.signed_word();
    for(unsigned int axis{0}; axis != 3; ++axis) aircraft.velocity[axis] = input.signed_word();
    landing.surface_height = input.signed_dword();
    aircraft.position.z = input.signed_dword();
    corncob::flight::angular_response response{
      .roll{input.signed_dword()},
    };
    aircraft.throttle = input.word();
    corncob::flight::time_step const step{
      .elapsed{input.signed_word()},
      .normal{5},
    };
    landing.runway = input.word() != 0;
    corncob::flight::control_input const controls{
      .brakes{input.word() != 0},
    };
    aircraft.on_ground = input.word() != 0;
    auto const result{corncob::flight::advance_landing(aircraft, response, landing, controls, step)};
    check(result == corncob::flight::landing_result::fatal, input.byte() != 0, "fatal suspension", i);
    for(unsigned int axis{0}; axis != 3; ++axis) check(aircraft.velocity[axis], input.signed_word(), "contact velocity", i);
    for(unsigned int axis{0}; axis != 3; ++axis) check(aircraft.body_velocity[axis], input.signed_word(), "contact body velocity", i);
    check(response.roll, input.signed_dword(), "wheel roll impulse", i);
    check(landing.spring_compression, input.signed_word(), "spring compression", i);
    check(landing.previous_vertical_speed, input.signed_word(), "vertical history", i);
    check(aircraft.on_ground, input.byte() != 0, "contact flag", i);
    check(landing.crash_landing, input.byte() != 0, "crash landing", i);
    check(landing.stopped, input.byte() != 0, "stopped", i);
  }
  corncob::tests::reference_reader recovery{argv[2], "CORNRCV1"};
  auto const recoveries{recovery.dword()};
  for(unsigned int i{0}; i != recoveries; ++i) {
    corncob::flight::aircraft_state aircraft;
    aircraft.position.z = -1000;
    aircraft.attitude.pitch = aircraft.attitude.roll = 16'384;
    for(unsigned int axis{0}; axis != 3; ++axis) aircraft.velocity[axis] = recovery.signed_word();
    aircraft.body_velocity.x = recovery.signed_word();
    corncob::flight::landing_state landing{
      .crash_landing{true},
    };
    corncob::flight::recover_invulnerable_aircraft(aircraft, landing);
    for(unsigned int axis{0}; axis != 3; ++axis) check(aircraft.velocity[axis], recovery.signed_word(), "invulnerable bounce", i);
    check(aircraft.position.z, int32_t{720}, "invulnerable reset height", i);
    check(aircraft.attitude.pitch, uint16_t{0}, "invulnerable reset pitch", i);
    check(aircraft.attitude.roll, uint16_t{0}, "invulnerable reset roll", i);
    check(landing.crash_landing || landing.stopped, false, "invulnerable crash state", i);
  }
  std::cout << magnitudes << " native magnitudes, " << wheels << " wheel tests and " << contacts << " landing passes match" << std::endl;
  return EXIT_SUCCESS;
}

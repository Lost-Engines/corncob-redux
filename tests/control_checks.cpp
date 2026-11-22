#include <bit>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include "flight/controls.h"
#include "tests/reference_reader.h"

namespace {

template<typename value_type>
void check(value_type const actual, value_type const expected, char const *const field, unsigned int const index) {
  /// Report the first mismatching native control accumulator or angle
  if(actual != expected) {
    throw std::runtime_error{std::string{field} + " case " + std::to_string(index)
      + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual)};
  }
}

corncob::maths::angles angles(corncob::tests::reference_reader &input) {
  /// Restore the explicit heading/pitch/roll order of the fixture
  return {
    .heading{input.word()},
    .pitch{input.word()},
    .roll{input.word()},
  };
}
void check_angles(corncob::maths::angles const &actual, corncob::tests::reference_reader &input, unsigned int const index) {
  /// Compare angles without masking differences caused by native table quantisation
  check(actual.heading, input.word(), "heading", index);
  check(actual.pitch, input.word(), "pitch", index);
  check(actual.roll, input.word(), "roll", index);
}

} // anonymous namespace

int main(int const argc, char const *const argv[]) {
  /// Compare aircraft control torque, matrix composition and attitude recovery with original machine code
  if(argc != 2) throw std::invalid_argument{"control_checks requires a fixture path"};
  corncob::tests::reference_reader input{argv[1], "CORNCT01"};
  auto const matrices{input.dword()};
  auto const controls{input.dword()};
  for(unsigned int i{0}; i != matrices; ++i) {
    auto const attitude{angles(input)};
    auto const change{angles(input)};
    auto const matrix{corncob::maths::make_inverse_rotation(attitude)};
    check_angles(corncob::maths::extract_angles(matrix), input, i);
    auto const rotated{corncob::maths::rotate_local(matrix, change)};
    for(auto const &row : rotated.rows) {
      for(unsigned int column{0}; column != 3; ++column) check(row[column], input.signed_word(), "local rotation", i);
    }
  }
  for(unsigned int i{0}; i != controls; ++i) {
    corncob::flight::aircraft_state aircraft;
    aircraft.attitude = angles(input);
    aircraft.world_to_body = corncob::maths::make_rotation(aircraft.attitude);
    aircraft.body_to_world = corncob::maths::make_inverse_rotation(aircraft.attitude);
    corncob::flight::control_input const stick{
      .roll{input.signed_word()},
      .pitch{input.signed_word()},
      .rudder{input.signed_word()},
    };
    for(unsigned int axis{0}; axis != 3; ++axis) {
      aircraft.body_velocity[axis] = input.signed_word();
      aircraft.scaled_body_velocity[axis] = static_cast<int16_t>(aircraft.body_velocity[axis] >> 4);
    }
    corncob::flight::angular_response response{
      .roll{input.signed_dword()},
      .pitch{input.signed_dword()},
      .yaw{input.signed_dword()},
      .wing_damage{input.signed_word()},
      .rudder_damage{input.signed_word()},
    };
    aircraft.density = input.word();
    corncob::flight::time_step const step{
      .elapsed{input.signed_word()},
      .normal{5},
    };
    aircraft.on_ground = input.word() != 0;
    aircraft.stalled = input.word() != 0;
    aircraft.flaps = input.word() != 0;
    response.stall_protection = input.word() != 0;
    aircraft.angle_of_attack = input.signed_word();
    aircraft.previous_angle_of_attack = input.signed_word();
    response.last_protected_pitch_rate = input.word();
    response.almost_stall_angle = input.word();
    aircraft.side_force = input.signed_word();
    corncob::flight::advance_controls(aircraft, response, stick, {}, step);
    check(response.roll, input.signed_dword(), "roll response", i);
    check(response.pitch, input.signed_dword(), "pitch response", i);
    check(response.yaw, input.signed_dword(), "yaw response", i);
    check(response.stall_protection, input.byte() != 0, "stall limiter", i);
    check(response.last_protected_pitch_rate, input.word(), "protected pitch rate", i);
    check(response.almost_stall_angle, input.word(), "stall threshold", i);
    check_angles(aircraft.attitude, input, i);
  }
  std::cout << matrices << " native attitude/rotation cases and " << controls << " control passes match" << std::endl;
  return EXIT_SUCCESS;
}

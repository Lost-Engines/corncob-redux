#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include "flight/controls.h"
#include "tests/reference_reader.h"

int main(int const argc, char const *const argv[]) {
  /// Match original autopilot feedback, pitch-table interpolation and the unsigned altitude-capture quirk
  if(argc != 2) throw std::invalid_argument{"autopilot_checks requires its native fixture"};
  corncob::tests::reference_reader input{argv[1], "CORNAUT2"};
  auto const count{input.dword()};
  for(unsigned int i{0}; i != count; ++i) {
    corncob::flight::aircraft_state aircraft;
    aircraft.velocity = corncob::maths::fixed_vector{input.signed_word(), input.signed_word(), input.signed_word()};
    aircraft.body_velocity.z = input.signed_word();
    aircraft.attitude.pitch = input.word();
    corncob::flight::angular_response response;
    response.pitch = input.signed_dword();
    response.autopilot.engaged = input.word() != 0;
    response.autopilot.height_locked = input.word() != 0;
    aircraft.position.z = input.signed_dword();
    response.autopilot.height = input.signed_dword();
    response.autopilot.previous_vertical_airflow = input.signed_word();
    response.beginner_flight = input.word() != 0;
    aircraft.stalled = input.word() != 0;
    corncob::flight::advance_autopilot(aircraft, response);
    auto const expected_pitch{input.signed_dword()};
    bool const engaged{input.byte() != 0};
    bool const locked{input.byte() != 0};
    auto const height{input.signed_dword()};
    auto const previous{input.signed_word()};
    if(response.pitch != expected_pitch || response.autopilot.engaged != engaged || response.autopilot.height_locked != locked
      || response.autopilot.height != height || response.autopilot.previous_vertical_airflow != previous) {
      throw std::runtime_error{"autopilot mismatch in native case " + std::to_string(i)};
    }
  }
  std::cout << count << " original autopilot feedback passes match" << std::endl;
  return EXIT_SUCCESS;
}

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include "game/pilot_motion.h"
#include "resources/binary_reader.h"

int main(int const argc, char const *const argv[]) {
  /// Verify native falling and parachute attitude arithmetic independently of window input and the aircraft simulation
  if(argc != 3) throw std::invalid_argument{"person and walking fixtures required"};
  std::ifstream input{argv[1], std::ios::binary};
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>{input}, {}};
  corncob::resources::binary_reader const data{bytes};
  if(bytes.size() != 26'640 || std::string{bytes.begin(), bytes.begin() + 8} != "CORNPER1") throw std::runtime_error{"invalid person fixture"};
  unsigned int offset{16};
  for(unsigned int i{0}; i != 512; ++i) {
    auto velocity{data.vector(offset)};
    corncob::game::damp_fall_velocity(velocity, data.signed_word(offset + 6), data.signed_word(offset + 8), {data.signed_word(offset + 10), 5});
    if(velocity != data.vector(offset + 12)) throw std::runtime_error{"native fall damping mismatch at case " + std::to_string(i)};
    offset += 18;
  }
  for(unsigned int i{0}; i != 512; ++i) {
    corncob::game::person_state person{
      .position{0, 0, 100'000},
      .attitude{
        .heading{data.word(offset)},
        .pitch{data.word(offset + 2)},
        .roll{data.word(offset + 4)},
      },
      .ejection_attitude{
        .heading{data.word(offset + 6)},
        .pitch{data.word(offset + 8)},
        .roll{data.word(offset + 10)},
      },
      .tumble{data.signed_word(offset + 12), data.signed_word(offset + 14)},
      .swing_frames{data.word(offset + 16)},
    };
    corncob::game::swing_parachute(person, {
      .roll{data.signed_word(offset + 18)},
      .pitch{data.signed_word(offset + 20)},
    });
    if(person.attitude.heading != data.word(offset + 22) || person.attitude.pitch != data.word(offset + 24) || person.attitude.roll != data.word(offset + 26)
      || person.tumble.x != data.signed_word(offset + 28) || person.tumble.y != data.signed_word(offset + 30) || person.swing_frames != data.word(offset + 32))
      throw std::runtime_error{"native parachute swing mismatch at case " + std::to_string(i)};
    offset += 34;
  }
  std::ifstream walking_input{argv[2], std::ios::binary};
  std::vector<uint8_t> walking_bytes{std::istreambuf_iterator<char>{walking_input}, {}};
  corncob::resources::binary_reader const walking{walking_bytes};
  if(walking_bytes.size() < 16 || std::string{walking_bytes.begin(), walking_bytes.begin() + 8} != "CORNWLK1"
    || walking_bytes.size() != 16 + walking.dword(8) * 14) throw std::runtime_error{"invalid walking fixture"};
  for(unsigned int i{0}; i < walking.dword(8); ++i) {
    unsigned int const offset{16 + i * 14};
    auto const delta{corncob::game::walking_displacement(walking.word(offset), {walking.signed_word(offset + 2), 0, 0}, {walking.signed_word(offset + 4), 5})};
    if(delta.x != walking.signed_dword(offset + 6) || delta.y != walking.signed_dword(offset + 10) || delta.z != 0)
      throw std::runtime_error{"native walking displacement mismatch at case " + std::to_string(i)};
  }
  std::cout << "512 original fall-damping, 512 parachute-swing and " << walking.dword(8) << " walking passes match" << std::endl;
  return EXIT_SUCCESS;
}

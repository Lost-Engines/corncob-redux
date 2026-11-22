#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include "maths/fixed.h"

namespace {

uint16_t read_word(std::istream &input) {
  /// Decode the fixture's explicit little-endian format on every host architecture
  uint16_t const low{static_cast<uint8_t>(input.get())};
  uint16_t const high{static_cast<uint8_t>(input.get())};
  return static_cast<uint16_t>(low | (high << 8));
}
uint32_t read_count(std::istream &input) {
  /// Read a recorded case count without relying on native structure packing
  uint32_t const low{read_word(input)};
  return low | (static_cast<uint32_t>(read_word(input)) << 16);
}

corncob::maths::rotation read_rotation(std::istream &input) {
  /// Restore one original row-major word matrix
  corncob::maths::rotation result;
  for(auto &row : result.rows) {
    row = corncob::maths::fixed_vector{
      std::bit_cast<int16_t>(read_word(input)),
      std::bit_cast<int16_t>(read_word(input)),
      std::bit_cast<int16_t>(read_word(input)),
    };
  }
  return result;
}

void check_word(int16_t const actual, std::istream &input, char const *const operation, unsigned int const index) {
  /// Report the first discrepancy against execution of the DOS routine
  int16_t const expected{std::bit_cast<int16_t>(read_word(input))};
  if(actual != expected) {
    throw std::runtime_error{std::string{operation} + " case " + std::to_string(index)
      + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual)};
  }
}
void check_rotation(corncob::maths::rotation const &actual, std::istream &input, char const *const operation, unsigned int const index) {
  /// Preserve row order while comparing all nine coefficients
  for(auto const &row : actual.rows) {
    for(unsigned int column{0}; column != 3; ++column) check_word(row[column], input, operation, index);
  }
}

} // anonymous namespace

auto main(int const argc, char const *const argv[])->int {
  /// Compare the translation with stored outputs from the unchanged 3.42 executable
  if(argc != 2) throw std::invalid_argument{"fixed_math_checks requires its reference fixture path"};
  std::ifstream input{argv[1], std::ios::binary};
  input.exceptions(std::ios::failbit | std::ios::badbit);
  std::array<char, 8> magic;
  input.read(magic.data(), magic.size());
  if(std::string_view{magic.data(), magic.size()} != "CORNREF1") throw std::runtime_error{"invalid native fixed-point fixture header"};
  unsigned int const sine_count{read_count(input)};
  unsigned int const rotation_count{read_count(input)};
  unsigned int const transform_count{read_count(input)};
  unsigned int const composition_count{read_count(input)};
  if(sine_count != 65'536) throw std::runtime_error{"native sine fixture must cover every angle"};
  for(unsigned int angle{0}; angle != sine_count; ++angle) {
    check_word(corncob::maths::sine(static_cast<corncob::maths::angle>(angle)), input, "sine", angle);
  }
  for(unsigned int i{0}; i != rotation_count; ++i) {
    corncob::maths::angles const angles{
      .heading{read_word(input)},
      .pitch{read_word(input)},
      .roll{read_word(input)},
    };
    check_rotation(corncob::maths::make_rotation(angles), input, "rotation", i);
    check_rotation(corncob::maths::make_inverse_rotation(angles), input, "inverse rotation", i);
  }
  for(unsigned int i{0}; i != transform_count; ++i) {
    auto const matrix{read_rotation(input)};
    corncob::maths::fixed_vector const vector{
      std::bit_cast<int16_t>(read_word(input)),
      std::bit_cast<int16_t>(read_word(input)),
      std::bit_cast<int16_t>(read_word(input)),
    };
    auto const saturated{corncob::maths::transform(matrix, vector)};
    auto const fast{corncob::maths::transform_vertex(matrix, vector)};
    for(unsigned int axis{0}; axis != 3; ++axis) check_word(saturated[axis], input, "saturated transform", i);
    for(unsigned int axis{0}; axis != 3; ++axis) check_word(fast[axis], input, "fast transform", i);
  }
  for(unsigned int i{0}; i != composition_count; ++i) {
    auto const lhs{read_rotation(input)};
    auto const rhs{read_rotation(input)};
    check_rotation(corncob::maths::compose(lhs, rhs), input, "matrix product", i);
  }
  std::cout << "All 65,536 native sine results, 512 matrix pairs, 512 transform pairs and 128 matrix products match" << std::endl;
  return EXIT_SUCCESS;
}

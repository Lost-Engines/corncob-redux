#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace corncob::tests {

class reference_reader {
private:
  std::ifstream input;

public:
  reference_reader(char const *const path, std::string_view const expected_magic) :
    input{path, std::ios::binary} {
    /// Fail on truncated data rather than treating EOF as a native arithmetic result
    input.exceptions(std::ios::failbit | std::ios::badbit);
    std::array<char, 8> magic;
    input.read(magic.data(), magic.size());
    if(std::string_view{magic.data(), magic.size()} != expected_magic) throw std::runtime_error{"invalid native fixture"};
  }
  uint8_t byte() {
    /// Read one native byte without relying on char signedness
    return static_cast<uint8_t>(input.get());
  }
  uint16_t word() {
    /// Decode the fixture's explicit little-endian word order
    unsigned int const low{byte()};
    unsigned int const high{byte()};
    return static_cast<uint16_t>(low | (high << 8));
  }
  int16_t signed_word() {
    /// Preserve the native sign bit
    return std::bit_cast<int16_t>(word());
  }
  uint32_t dword() {
    /// Decode a count or dword without host structure padding
    uint32_t const low{word()};
    return low | (static_cast<uint32_t>(word()) << 16);
  }
  int32_t signed_dword() {
    /// Read signed world positions and force accumulators
    return std::bit_cast<int32_t>(dword());
  }
};

} // namespace corncob::tests

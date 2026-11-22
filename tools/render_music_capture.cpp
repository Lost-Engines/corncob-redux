// Development-only renderer for unchanged DOSBox OPL2 register captures.
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <opl3.h>

int main(int const argc, char const *const argv[]) try {
  /// Render one complete native score through the pinned OPL emulator, without a second ROL sequencer
  if(argc != 3) throw std::invalid_argument{"expected capture.dro and its native repeat boundary in milliseconds"};
  unsigned int end_ms{0};
  std::string_view const end_text{argv[2]};
  auto const parsed{std::from_chars(end_text.begin(), end_text.end(), end_ms)};
  if(parsed.ec != std::errc{} || parsed.ptr != end_text.end() || end_ms == 0 || end_ms > 600'000) {
    throw std::invalid_argument{"invalid native song boundary"};
  }
  std::ifstream file{argv[1], std::ios::binary};
  std::vector<uint8_t> const bytes{std::istreambuf_iterator<char>{file}, {}};
  auto const word{[&](unsigned int const offset){return bytes.at(offset) | bytes.at(offset + 1) << 8;}};
  if(bytes.size() < 26 || std::string_view{reinterpret_cast<char const*>(bytes.data()), 8} != "DBRAWOPL"
    || word(8) != 2 || word(10) != 0 || bytes[20] != 0 || bytes[21] != 0 || bytes[22] != 0
    || bytes[25] > 127 || bytes.size() < 26u + bytes[25] || ((bytes.size() - 26 - bytes[25]) & 1u) != 0) {
    throw std::invalid_argument{"expected an uncompressed DOSBox DRO v2 OPL2 capture"};
  }
  opl3_chip chip{};
  unsigned int constexpr sample_rate{48'000};
  OPL3_Reset(&chip, sample_rate);
  uint64_t rendered{0};
  auto const render_until{[&](uint64_t const frame){
    while(rendered < frame) {
      std::array<int16_t, 2> output;
      OPL3_GenerateResampled(&chip, output.data());
      // AdLib is mono. Preserve the left DAC path without gain changes or normalisation.
      auto const sample{static_cast<uint16_t>(output[0])};
      std::cout.put(static_cast<char>(sample & 255));
      std::cout.put(static_cast<char>(sample >> 8));
      ++rendered;
    }
  }};
  unsigned int milliseconds{0}, writes{0};
  for(unsigned int offset{26u + bytes[25]}; offset < bytes.size(); offset += 2) {
    uint8_t const command{bytes[offset]}, value{bytes[offset + 1]};
    if(command == bytes[23]) milliseconds += value + 1;
    else if(command == bytes[24]) milliseconds += (value + 1) * 256;
    else {
      if(milliseconds >= end_ms) break;
      render_until(static_cast<uint64_t>(milliseconds) * sample_rate / 1000);
      if((command & 127u) >= bytes[25]) throw std::invalid_argument{"invalid DRO register-table index"};
      // DOSBox's initial cache can include the unused second bank even for an OPL2 capture.
      if((command & 128u) == 0) {
        OPL3_WriteReg(&chip, bytes[26 + command], value);
        ++writes;
      }
    }
  }
  if(milliseconds < end_ms) throw std::invalid_argument{"capture ends before the native repeat boundary"};
  render_until(static_cast<uint64_t>(end_ms) * sample_rate / 1000);
  if(!std::cout) throw std::runtime_error{"cannot write music PCM"};
  std::cerr << writes << " original register writes, " << rendered << " mono frames\n";
  return EXIT_SUCCESS;
} catch(std::exception const &error) {
  std::cerr << error.what() << '\n';
  return EXIT_FAILURE;
}

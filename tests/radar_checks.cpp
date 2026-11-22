#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include "graphics/instruments.h"
#include "graphics/radar.h"
#include "resources/binary_reader.h"

int main(int const argc, char const *const argv[]) {
  /// Compare the original radar's coordinate protocol and every possible compressed distance input
  if(argc != 2) throw std::invalid_argument{"radar fixture required"};
  std::ifstream input{argv[1], std::ios::binary};
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>{input}, {}};
  corncob::resources::binary_reader const data{bytes};
  if(bytes.size() != 69'644 || std::string{bytes.begin(), bytes.begin() + 8} != "CORNRAD1") throw std::runtime_error{"invalid radar fixture"};
  unsigned int offset{12};
  for(unsigned int i{0}; i != 512; ++i) {
    auto const position{corncob::graphics::radar_position(data.word(offset), data.word(offset + 2))};
    if(position.x != data.signed_word(offset + 4) || position.y != data.signed_word(offset + 6)) throw std::runtime_error{"native radar coordinate mismatch"};
    offset += 8;
  }
  for(unsigned int i{0}; i != 32'768; ++i) {
    if(corncob::maths::square_root(i) != data.word(offset)) throw std::runtime_error{"native radar square-root mismatch at " + std::to_string(i)};
    offset += 2;
  }
  auto const front{corncob::graphics::radar_position({100'000, 0, 0}, {0, 0, 0}, 0)};
  auto const right{corncob::graphics::radar_position({0, -100'000, 0}, {0, 0, 0}, 0)};
  auto const turned{corncob::graphics::radar_position({0, -100'000, 0}, {0, 0, 0}, static_cast<uint16_t>(-16'384))};
  if(!front || front->y >= 295 || !right || right->x <= 561 || !turned || turned->y >= 295) throw std::runtime_error{"radar does not follow aircraft bearing"};
  if(corncob::graphics::radar_position({524'288, 0, 0}, {0, 0, 0}, 0)) throw std::runtime_error{"radar accepted an out-of-range contact"};
  corncob::graphics::indexed_framebuffer panel;
  corncob::maths::random_source random;
  corncob::graphics::instrument_panel instruments;
  corncob::flight::aircraft_state aircraft;
  corncob::graphics::instrument_telemetry const telemetry{
    .aircraft{aircraft},
    .controls{},
  };
  aircraft.body_velocity = corncob::maths::fixed_vector{0, 0, 0};
  std::ranges::fill(panel.pixels, 0);
  instruments.draw(panel, random, telemetry, 0);
  if(std::ranges::find(panel.pixels, 12) != panel.pixels.end()) throw std::runtime_error{"stationary airflow indicator escaped its instrument window"};
  aircraft.body_velocity = corncob::maths::fixed_vector{1000, 0, 0};
  std::ranges::fill(panel.pixels, 0);
  instruments.draw(panel, random, telemetry, 0);
  if(std::ranges::find(panel.pixels, 12) == panel.pixels.end()) throw std::runtime_error{"valid airflow indicator was hidden"};
  std::cout << "512 native radar positions and all 32768 compressed distance inputs match" << std::endl;
  return EXIT_SUCCESS;
}

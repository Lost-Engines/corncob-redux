#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include "graphics/instruments.h"
#include "tests/reference_reader.h"

int main(int const argc, char const *const argv[]) {
  /// Compare complete original colour-search masks, including every row/column beyond each button
  if(argc != 3) throw std::invalid_argument{"cockpit-button fixture required"};
  corncob::tests::reference_reader input{argv[1], "CORNBTN1"};
  unsigned int const cases{input.dword()};
  corncob::graphics::indexed_framebuffer target;
  for(unsigned int i{0}; i != cases; ++i) {
    auto const button{static_cast<corncob::graphics::cockpit_button>(input.byte())};
    auto const source{input.byte()}, destination{input.byte()};
    uint64_t const low{input.dword()}, high{input.dword()};
    for(unsigned int y{0}; y != 350; ++y) {
      for(unsigned int x{0}; x != 640; ++x) target.pixels[y * 640 + x] = static_cast<uint8_t>((x * 5 + y * 7) & 15u);
    }
    corncob::graphics::recolour_button(target, button, source, destination);
    uint64_t hash{14'695'981'039'346'656'037ull};
    for(auto const pixel : target.pixels) hash = (hash ^ pixel) * 1'099'511'628'211ull;
    if(hash != (high << 32 | low)) throw std::runtime_error{"native cockpit-button pixels differ in case " + std::to_string(i)};
  }
  corncob::tests::reference_reader sights{argv[2], "CORNGSN1"};
  unsigned int const sight_cases{sights.dword()};
  for(unsigned int i{0}; i != sight_cases; ++i) {
    corncob::graphics::clip_rectangle const bounds{
      .left{sights.signed_word()},
      .top{sights.signed_word()},
      .right{sights.signed_word()},
      .bottom{sights.signed_word()},
    };
    corncob::graphics::screen_point const centre{sights.signed_word(), sights.signed_word()};
    uint64_t const low{sights.dword()}, high{sights.dword()};
    std::ranges::fill(target.pixels, 2);
    corncob::maths::random_source random;
    corncob::graphics::draw_gunsight(target, random, bounds, centre);
    uint64_t hash{14'695'981'039'346'656'037ull};
    for(auto const pixel : target.pixels) hash = (hash ^ pixel) * 1'099'511'628'211ull;
    if(hash != (high << 32 | low)) throw std::runtime_error{"native gunsight pixels differ"};
  }
  std::cout << cases << " complete original cockpit colour-search masks match" << std::endl;
  return EXIT_SUCCESS;
}

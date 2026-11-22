#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "graphics/title_sequence.h"
#include "resources/assets.h"
#include "tests/reference_reader.h"

namespace {

corncob::graphics::title_motion read(corncob::tests::reference_reader &input) {
  /// Import independent native motion state without relying on C++ object layout
  return {
    .stage{static_cast<corncob::graphics::title_stage>(input.byte())},
    .frame{input.word()},
    .fade{input.signed_word()},
    .sky_intensity{input.signed_word()},
    .terrain_scroll{input.signed_word()},
    .cloud_scroll{input.signed_word()},
    .aircraft_y{input.signed_word()},
    .passes{input.signed_word()},
    .first_pass{input.byte() != 0},
    .saucer_frame{input.signed_word()},
    .saucer{input.signed_word(), input.signed_word()},
    .explosion_timer{input.signed_word()},
    .explosion{input.signed_word(), input.signed_word()},
    .explosion_frame{input.signed_word()},
    .mask_threshold{input.signed_word()},
    .credits_row{input.signed_word()},
    .random_seed{input.dword()},
  };
}

} // anonymous namespace

int main(int const argc, char const *const argv[]) {
  /// Compare the unchanged native animation loops, then exercise a complete animated presentation cycle
  if(argc < 2 || argc > 3) throw std::invalid_argument{"title fixture required"};
  corncob::tests::reference_reader input{argv[1], "CORNTTL1"};
  unsigned int const count{input.dword()};
  unsigned int const masks{input.dword()};
  for(unsigned int i{0}; i != count; ++i) {
    auto actual{read(input)};
    auto const expected{read(input)};
    bool const finished{input.byte() != 0};
    auto const frame{actual.advance()};
    auto check{[i](auto const &value, auto const &native, char const *const name){
      if(value != native) throw std::runtime_error{"native title case " + std::to_string(i) + " differs: " + name};
    }};
    check(actual.frame, expected.frame, "frame");
    check(actual.fade, expected.fade, "fade");
    check(actual.sky_intensity, expected.sky_intensity, "sky intensity");
    check(actual.terrain_scroll, expected.terrain_scroll, "terrain scroll");
    check(actual.cloud_scroll, expected.cloud_scroll, "cloud scroll");
    check(actual.aircraft_y, expected.aircraft_y, "aircraft");
    check(actual.passes, expected.passes, "passes");
    check(actual.first_pass, expected.first_pass, "first pass");
    check(actual.saucer_frame, expected.saucer_frame, "saucer frame");
    check(actual.saucer, expected.saucer, "saucer position");
    check(actual.explosion_timer, expected.explosion_timer, "explosion timer");
    check(actual.explosion, expected.explosion, "explosion position");
    check(actual.explosion_frame, expected.explosion_frame, "explosion frame");
    check(actual.mask_threshold, expected.mask_threshold, "letter mask");
    check(actual.credits_row, expected.credits_row, "credits");
    check(actual.random_seed, expected.random_seed, "random continuation");
    check(frame.stage_finished, finished, "stage completion");
  }
  for(unsigned int i{0}; i != masks; ++i) {
    uint8_t const threshold{input.byte()};
    unsigned int const length{input.word()};
    std::vector<uint8_t> cloud(length);
    std::vector<uint8_t> stencil(length);
    for(auto &value : cloud) value = input.byte();
    for(auto &value : stencil) value = input.byte();
    for(unsigned int j{0}; j != length; ++j) {
      if(corncob::graphics::title_letter_colour(cloud[j], stencil[j], threshold) != input.byte()) throw std::runtime_error{"native VGA title mask differs"};
    }
  }
  corncob::resources::collection assets{corncob::resources::front_end_asset()};
  corncob::graphics::title_sequence title{assets};
  corncob::render::framebuffer image;
  if(argc == 3) std::filesystem::create_directories(argv[2]);
  for(unsigned int i{0}; i != 800; ++i) {
    title.advance(1.0 / 20.0);
    title.render(image);
    if(argc == 3 && (i == 30 || i == 60 || i == 120 || i == 240 || i == 280 || i == 350)) {
      std::ofstream output{std::filesystem::path{argv[2]} / ("title-" + std::to_string(i) + ".ppm"), std::ios::binary};
      output << "P6\n320 200\n255\n";
      for(auto const &pixel : image.pixels) {
        output.put(static_cast<char>(pixel.red));
        output.put(static_cast<char>(pixel.green));
        output.put(static_cast<char>(pixel.blue));
      }
    }
  }
  if(image.width != 320 || image.height != 200) throw std::runtime_error{"animated title changed video resolution"};
  std::cout << count << " native title motion states, 6144 original VGA mask pixels and a complete rendered presentation cycle exercised" << std::endl;
  return EXIT_SUCCESS;
}

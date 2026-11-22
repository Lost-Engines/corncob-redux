#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include "game/vision.h"
#include "graphics/scene.h"
#include "render/palette.h"
#include "resources/assets.h"
#include "tests/reference_reader.h"

int main(int const argc, char const *const argv[]) {
  /// Compare visual state with unchanged DOS routines and every native DAC write, then check the palette reaches the scene renderer
  if(argc != 2) throw std::invalid_argument{"visual feedback fixture required"};
  corncob::tests::reference_reader input{argv[1], "CORNVIS1"};
  auto const states{input.dword()};
  auto const palettes{input.dword()};
  for(unsigned int i{0}; i != states; ++i) {
    int32_t const force{input.signed_dword()};
    int32_t const average{input.signed_dword()};
    uint16_t const brightness{input.word()};
    uint16_t const frame{input.word()};
    corncob::flight::time_step const step{
      .elapsed{input.signed_word()},
      .normal{input.signed_word()},
    };
    bool const on_foot{input.byte() != 0};
    bool const closing{input.byte() != 0};
    corncob::game::vision_state actual{
      .average_vertical_force{average},
      .brightness{brightness},
    };
    actual.advance(force, on_foot, frame, step);
    if(closing) actual.close_capture(step);
    int32_t const expected_average{input.signed_dword()};
    uint16_t const expected_brightness{input.word()};
    if(actual.average_vertical_force != expected_average || actual.brightness != expected_brightness) {
      throw std::runtime_error{"native visual-state case " + std::to_string(i) + ": average " + std::to_string(actual.average_vertical_force)
        + "/" + std::to_string(expected_average) + ", intensity " + std::to_string(actual.brightness) + "/" + std::to_string(expected_brightness)};
    }
  }
  auto expand{[](uint8_t const value){return static_cast<uint8_t>((value << 2) | (value >> 4));}};
  for(unsigned int i{0}; i != palettes; ++i) {
    auto const brightness{static_cast<uint8_t>(input.word())};
    std::array<corncob::render::rgba_pixel, 16> colours;
    for(auto &colour : colours) {
      colour = {
        .red{expand(input.byte())},
        .green{expand(input.byte())},
        .blue{expand(input.byte())},
      };
    }
    corncob::render::fade_palette(colours, brightness);
    for(auto const &colour : colours) {
      auto const red{expand(input.byte())};
      auto const green{expand(input.byte())};
      auto const blue{expand(input.byte())};
      if(colour.red != red || colour.green != green || colour.blue != blue) throw std::runtime_error{"native DAC palette case " + std::to_string(i)};
    }
  }
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset()};
  auto const cockpit{corncob::resources::decode_cockpit_mask(corncob::resources::cockpit_asset())};
  corncob::maths::random_source scene_random;
  corncob::graphics::scene_renderer renderer{geometry, cockpit};
  corncob::render::framebuffer image;
  corncob::graphics::camera observer{
    .attitude{},
    .cockpit{false},
    .rear_view{false},
    .ground_detail{false},
    .palette_brightness{0},
  };
  renderer.render({}, observer, 0, 0, scene_random, image);
  if(!std::ranges::all_of(image.pixels, [](auto const &pixel){return pixel.red == 0 && pixel.green == 0 && pixel.blue == 0;})) throw std::runtime_error{"zero visual intensity did not reach the scene renderer"};
  observer.palette_brightness = 255;
  renderer.render({}, observer, 1, 40, scene_random, image);
  if(!std::ranges::any_of(image.pixels, [](auto const &pixel){return pixel.red != 0 || pixel.green != 0 || pixel.blue != 0;})) throw std::runtime_error{"normal scene intensity remained black"};
  std::cout << states << " native visual-state cases and " << palettes << " full palettes match; scene intensity connected" << std::endl;
  return EXIT_SUCCESS;
}

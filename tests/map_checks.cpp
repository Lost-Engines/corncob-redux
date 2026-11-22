#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include "game/session.h"
#include "graphics/map_projection.h"
#include "resources/assets.h"
#include "resources/binary_reader.h"

int main(int const argc, char const *const argv[]) {
  /// Match native map projection, then verify navigation cannot alter the frozen sortie
  if(argc < 2 || argc > 3) throw std::invalid_argument{"map fixture required"};
  std::ifstream file{argv[1], std::ios::binary};
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>{file}, {}};
  corncob::resources::binary_reader const data{bytes};
  if(bytes.size() != 12'300 || std::string{bytes.begin(), bytes.begin() + 8} != "CORNMAP1") throw std::runtime_error{"invalid map fixture"};
  unsigned int offset{12};
  for(unsigned int i{0}; i != 512; ++i) {
    auto const result{corncob::graphics::map_relative({data.signed_dword(offset), data.signed_dword(offset + 4), data.signed_dword(offset + 8)})};
    for(unsigned int axis{0}; axis != 3; ++axis) {
      if(result[axis] != data.signed_dword(offset + 12 + axis * 4)) throw std::runtime_error{"native map projection mismatch at case " + std::to_string(i)};
    }
    offset += 24;
  }
  namespace game = corncob::game;
  namespace resources = corncob::resources;
  resources::geometry_bank geometry{resources::engine_data_asset()};
  game::session flight{geometry, corncob::world::decode_theatre(resources::morning_asset())};
  auto const position{flight.pilot().position};
  auto const initial_frame{flight.frame_number()};
  flight.press(game::action::toggle_map);
  auto const start{flight.camera()};
  if(!flight.in_map() || !start.map_mode || start.cockpit || start.position.z != 210 * 65'536) throw std::runtime_error{"map did not start above the retained pilot"};
  for(unsigned int i{0}; i != 20; ++i) {
    flight.advance({
      .stick{
        .roll{512},
        .pitch{-512},
      },
      .map{
        .lower{true},
      },
    });
  }
  if(flight.pilot().position != position || flight.frame_number() != initial_frame || flight.camera().position.z >= start.position.z || flight.camera().position.x <= start.position.x) throw std::runtime_error{"map navigation advanced or relocated the sortie"};
  flight.advance({.map{.home{true}}});
  if(flight.camera().position.x != position.x || flight.camera().position.y != position.y) throw std::runtime_error{"map Home did not restore the pilot location"};
  auto const cockpit{resources::decode_cockpit_mask(resources::cockpit_asset())};
  corncob::maths::random_source scene_random;
  corncob::graphics::scene_renderer renderer{geometry, cockpit};
  corncob::render::framebuffer image;
  auto const rendered{renderer.render(flight.world(), flight.camera(), 1, 512, scene_random, image)};
  if(rendered.objects == 0) throw std::runtime_error{"map rendered no world placements"};
  if(argc == 3) {
    std::ofstream capture{argv[2], std::ios::binary};
    capture << "P6\n640 350\n255\n";
    for(auto const &pixel : image.pixels) {
      capture.put(static_cast<char>(pixel.red));
      capture.put(static_cast<char>(pixel.green));
      capture.put(static_cast<char>(pixel.blue));
    }
  }
  flight.press(game::action::toggle_map);
  if(flight.in_map() || flight.camera().position != position) throw std::runtime_error{"leaving map lost the retained pilot"};
  std::cout << "512 original map reductions and frozen-sortie navigation/rendering exercised" << std::endl;
  return EXIT_SUCCESS;
}

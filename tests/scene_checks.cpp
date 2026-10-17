#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include "graphics/scene.h"
#include "resources/assets.h"
#include "tests/reference/native_tower.h"
#include "tests/reference_reader.h"

int main(int const argc, char const *const argv[]) {
  if(argc != 2) throw std::invalid_argument{"native scene-order fixture required"};
  /// Exercise every theatre across octants, near-plane intersections and cockpit/rear view layouts
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset()};
  auto const cockpit{corncob::resources::decode_cockpit_mask(corncob::resources::cockpit_asset())};
  (void)argv;
  corncob::maths::random_source scene_random;
  corncob::graphics::scene_renderer renderer{geometry, cockpit};
  corncob::render::framebuffer output;
  renderer.configure({.resolution{160,100}, .ground_points{0}});
  unsigned int frames{0}, objects{0}, polygons{0}, discs{0};
  for(auto const data : {corncob::resources::white_sands_asset(), corncob::resources::morning_asset(),
                        corncob::resources::dusk_asset(), corncob::resources::wasteland_asset()}) {
    auto const theatre{corncob::world::decode_theatre(data)};
    corncob::graphics::camera observer{
      .position{corncob::maths::world_position{theatre.start}},
      .attitude{},
    };
    for(auto const &object : theatre.objects) {
      observer.position = corncob::maths::world_position{object.position};
      observer.position.x -= 800;
      observer.position.z += 1500;
      for(unsigned int quadrant{0}; quadrant != 4; ++quadrant) {
        observer.attitude = {
          .heading{static_cast<corncob::maths::angle>(quadrant * 16'384)},
          .pitch{static_cast<corncob::maths::angle>(quadrant * 2048)},
          .roll{static_cast<corncob::maths::angle>(quadrant * 8192)},
        };
        observer.cockpit = (quadrant & 1u) == 0;
        observer.rear_view = quadrant == 0;
        auto const drawn{renderer.render(theatre, observer, frames++, 1024, scene_random, output)};
        objects += drawn.objects;
        polygons += drawn.polygons;
        discs += drawn.discs;
      }
    }
  }
  if(objects == 0 || polygons == 0 || discs == 0) throw std::runtime_error{"theatre sweep failed to exercise the scene renderer"};
  std::cout << "Depth-tested theatre sweep: rendered " << frames << " original-theatre views: " << objects << " objects, " << polygons
    << " polygons, " << discs << " discs" << std::endl;
  return EXIT_SUCCESS;
}

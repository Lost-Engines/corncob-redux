#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include "game/session.h"
#include "resources/assets.h"
#include "resources/collection.h"
#include "world/theatre_catalog.h"

int main(int const argc, char const *const argv[]) {
  if(argc != 2) throw std::invalid_argument{"recording_checks requires its independently decoded references"};
  namespace game = corncob::game;
  namespace resources = corncob::resources;
  resources::collection const assets{resources::other_worlds_asset()};
  resources::geometry_bank geometry{resources::engine_data_asset()};
  std::ifstream references{argv[1]};
  std::string name;
  unsigned int count{0};
  unsigned int compared{0};
  uint64_t expected{0};
  while(references >> name >> std::dec >> count >> std::hex >> expected) {
    auto const bytes{assets.file("dmo/" + name + ".dmo")};
    game::recorded_flight recording{bytes};
    if(recording.frames().size() != count) throw std::runtime_error{"recorded flight lost frames"};
    uint64_t hash{0xcbf29ce484222325ull};
    auto value{[&](uint32_t const number, unsigned int const size){
      for(unsigned int i{0}; i != size; ++i) hash = (hash ^ static_cast<uint8_t>(number >> (i * 8))) * 0x100000001b3ull;
    }};
    for(auto const &frame : recording.frames()) {
      for(unsigned int axis{0}; axis != 3; ++axis) value(static_cast<uint32_t>(frame.position[axis]), 4);
      for(auto const angle : {frame.attitude.heading, frame.attitude.pitch, frame.attitude.roll}) value(angle, 2);
      for(unsigned int axis{0}; axis != 3; ++axis) value(static_cast<uint32_t>(frame.velocity[axis]), 4);
      value(frame.buttons, 1);
    }
    if(hash != expected) throw std::runtime_error{"recorded positions, angles, velocities or event bytes differ from the independent original-format decoder"};
    auto flight{std::make_unique<game::session>(geometry, corncob::world::load_theatre("other_worlds/demo1"))};
    flight->begin_recording(bytes);
    for(unsigned int step{0}; step != 60; ++step) {
      flight->advance({});
      auto const &expected_frame{recording.frames()[step / 2]};
      if(flight->craft().position != expected_frame.position) throw std::runtime_error{"playback drifted away from the stored aircraft path"};
    }
    bool rejected{false};
    try {
      game::recorded_flight truncated{bytes.first(bytes.size() - 1)};
    }
    catch(std::invalid_argument const &) {
      rejected = true;
    }
    if(!rejected) throw std::runtime_error{"truncated recording was accepted"};
    ++compared;
  }
  if(compared != 3) throw std::runtime_error{"missing original recording references"};
  std::cout << "All three complete recordings match independent decoding; replay positions and malformed input checks pass\n";
}

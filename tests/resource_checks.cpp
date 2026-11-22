#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include "render/framebuffer.h"
#include "resources/assets.h"
#include "resources/cockpit_mask.h"
#include "resources/geometry_bank.h"
#include "world/world.h"

namespace {

void require(bool const condition, char const *const message) {
  /// Keep reference checks active in Release as well as assertion-enabled builds
  if(!condition) throw std::runtime_error{message};
}

} // anonymous namespace

auto main(int const argc, char const *const argv[])->int {
  if(argc != 2) throw std::invalid_argument{"legacy saved-world fixture required"};
  /// Compare decompression with the mask already verified against the native RIMG routine
  auto const decoded{corncob::resources::decode_cockpit_mask(corncob::resources::cockpit_asset())};
  uint64_t hash{14'695'981'039'346'656'037ull};
  uint64_t constexpr fnv_prime{1'099'511'628'211ull};
  for(auto const byte : decoded) hash = (hash ^ byte) * fnv_prime;
  require(hash == 0x4f'f0'eb'ad'7c'69'b6'47ull, "embedded cockpit differs from the native-verified 28,000-byte mask");

  // a repeat can fill the destination before the stream supplies another literal count
  std::array<uint8_t, 14> const final_repeat{0, 0, 0, 0, 0xff, 0xff, 0xa5, 0, 0, 0xff, 0xff, 0xa5, 0, 0};
  auto const repeated{corncob::resources::decode_cockpit_mask(final_repeat)};
  require(std::ranges::all_of(repeated, [](uint8_t const value){
    return value == 0xa5;
  }), "destination-clipped final repeat was not decoded");

  bool rejected{false};
  try {
    corncob::resources::decode_cockpit_mask(std::array<uint8_t, 4>{0, 0, 0, 0});
  } catch(std::invalid_argument const&) {
    rejected = true;
  }
  require(rejected, "truncated cockpit IMG was accepted");
  auto const desktop{corncob::render::fit_viewport(1920, 1080)};
  require(desktop.width == 1920 && desktop.height == 1050 && desktop.x == 0 && desktop.y == 15, "integer flight scaling does not fit the desktop");
  auto const menus{corncob::render::fit_viewport(1920, 1200, 640, 400)};
  require(menus.width == 1920 && menus.height == 1200 && menus.x == 0 && menus.y == 0, "scale-three menus do not match uncorrected DOSBox");
  auto const resized{corncob::render::fit_viewport(1365, 1000, 640, 400)};
  require(resized.width == 1280 && resized.height == 800 && resized.x == 42 && resized.y == 100, "resizing introduced uneven enlarged text pixels");
  auto const title{corncob::render::fit_viewport(1280, 900, 320, 200)};
  require(title.width == 1280 && title.height == 800 && title.y == 50, "VGA artwork does not use its actual pixel grid");
  auto const crt{corncob::render::fit_viewport(1920, 1080, 640, 350, corncob::render::display_aspect::crt)};
  require(crt.width == 1280 && crt.height == 960 && crt.x == 320 && crt.y == 60, "optional CRT correction lost its 4:3 ratio");
  auto const small{corncob::render::fit_viewport(320, 240)};
  require(small.width == 320 && small.height == 175, "small-window downscaling changed the source aspect ratio");
  require(corncob::render::fit_viewport(0, 480).width == 0 && corncob::render::fit_viewport(640, 480, 0, 350).height == 0, "empty video modes have a visible viewport");
  corncob::resources::geometry_bank const geometry{corncob::resources::engine_data_asset()};
  auto const &aircraft{geometry.definition(corncob::world::object_type::aircraft)};
  require(aircraft.models.near == 0x4aa9 && aircraft.hit_points == 2, "embedded aircraft definition differs from the original template");
  std::array const sources{corncob::resources::white_sands_asset(), corncob::resources::morning_asset(),
                           corncob::resources::dusk_asset(), corncob::resources::wasteland_asset()};
  std::array<unsigned int, 4> const expected_counts{51, 221, 169, 185};
  for(unsigned int i{0}; i != sources.size(); ++i) {
    auto const world{corncob::world::decode_theatre(sources[i])};
    require(world.objects.size() == expected_counts[i], "original theatre object count changed during decoding");
    require(world.start.x == -848'968 && world.start.y == 0 && world.start.z == 720, "original theatre start trailer changed during decoding");
    auto const encoded{corncob::world::encode_theatre(world)};
    require(std::ranges::equal(encoded, sources[i]), "native theatre stream did not round trip byte for byte");
    unsigned int total{0};
    for(auto const &tile : world.tiles) total += tile.object_count;
    require(total == world.objects.size(), "theatre tile ranges do not cover every placed object");
  }
  std::ifstream legacy_file{argv[1], std::ios::binary};
  std::vector<uint8_t> const legacy_bytes{std::istreambuf_iterator<char>{legacy_file}, {}};
  auto const legacy{corncob::world::decode_theatre(legacy_bytes)};
  require(legacy.objects.size() == 252, "original plain-type saved world was not decoded");
  auto const canonical{corncob::world::encode_theatre(legacy)};
  uint64_t canonical_hash{14'695'981'039'346'656'037ull};
  for(auto const byte : canonical) canonical_hash = (canonical_hash ^ byte) * fnv_prime;
  require(canonical_hash == 0x95c4046aada506dfull, "plain saved-world conversion changed original packed fields");
  require(corncob::world::encode_theatre(corncob::world::decode_theatre(canonical)) == canonical, "legacy save did not round trip through the current encoding");
  std::cout << "Embedded cockpit, all 61 model definitions, four original theatres and display checks pass" << std::endl;
  return EXIT_SUCCESS;
}

#include "world.h"
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <utility>
#include "resources/binary_reader.h"

namespace corncob::world {

bool in_active_neighbourhood(maths::world_position const &position, maths::world_position const &observer) noexcept {
  /// UPDATECT streams the four tiles surrounding a half-tile-rounded observer centre
  for(unsigned int axis{0}; axis != 2; ++axis) {
    int const centre{((observer[axis] >> 16) - (theatre::origin >> 16) + 16) / 32};
    int const tile{((position[axis] >> 16) - (theatre::origin >> 16)) / 32};
    if(tile < 0 || tile >= 8 || centre - tile < 0 || centre - tile > 1) return false;
  }
  return true;
}

namespace {

enum class type_encoding {
  coordinates_xor,
  plain
};

theatre decode_tiles(std::span<uint8_t const> const source, type_encoding const encoding) {
  /// Expand the original 64-tile CCT stream while retaining its tile and palette boundaries
  unsigned int constexpr header_bytes{16};
  unsigned int constexpr record_bytes{23};
  resources::binary_reader const input{source};
  theatre result;
  size_t offset{0};
  for(unsigned int y{0}; y != theatre::rows; ++y) {
    for(unsigned int x{0}; x != theatre::columns; ++x) {
      if(input.dword(offset) != 0x00'65'69'50u || input.word(offset + 4) != x || input.word(offset + 6) != y) {
        throw std::invalid_argument{"embedded theatre has an invalid tile header"};
      }
      auto &tile{result.tiles[y * theatre::columns + x]};
      tile.first_object = static_cast<unsigned int>(result.objects.size());
      tile.object_count = input.word(offset + 8);
      tile.storage_flags = input.byte(offset + 10);
      for(unsigned int i{0}; i != tile.packed_colours.size(); ++i) tile.packed_colours[i] = input.byte(offset + 11 + i);
      offset += header_bytes;
      for(unsigned int i{0}; i != tile.object_count; ++i) {
        auto const packed{input.word(offset)};
        auto const low_x{input.word(offset + 2)};
        auto const low_y{input.word(offset + 4)};
        auto const low_z{input.word(offset + 6)};
        auto const kind{static_cast<uint16_t>(input.word(offset + 21) ^ (encoding == type_encoding::coordinates_xor ? low_x ^ low_y : 0))};
        if(kind >= std::to_underlying(object_type::count)) throw std::invalid_argument{"embedded theatre contains an invalid object type"};
        result.objects.push_back({
          .position{
            theatre::origin + static_cast<int32_t>(x) * theatre::tile_size + static_cast<int32_t>(packed & 31) * 65'536 + low_x,
            theatre::origin + static_cast<int32_t>(y) * theatre::tile_size + static_cast<int32_t>((packed >> 5) & 31) * 65'536 + low_y,
            static_cast<int32_t>((packed >> 10) & 31) * 65'536 + low_z,
          },
          .attitude{
            .heading{static_cast<maths::angle>(input.byte(offset + 8) << 8)},
            .pitch{static_cast<maths::angle>(input.byte(offset + 9) << 8)},
            .roll{static_cast<maths::angle>(input.byte(offset + 10) << 8)},
          },
          .velocity{input.signed_word(offset + 13), input.signed_word(offset + 15), input.signed_word(offset + 17)},
          .type{static_cast<object_type>(kind)},
          .flags{input.word(offset + 11)},
          .timer{input.word(offset + 19)},
        });
        offset += record_bytes;
      }
    }
  }
  if(offset > source.size() || source.size() - offset != 12) throw std::invalid_argument{"embedded theatre has an invalid start-position trailer"};
  result.start = maths::world_position{input.signed_dword(offset), input.signed_dword(offset + 4), input.signed_dword(offset + 8)};
  return result;
}

} // anonymous namespace

theatre decode_theatre(std::span<uint8_t const> const source) {
  /// Older saves store plain type words; accept a format only after its entire 64-tile stream validates
  try {
    return decode_tiles(source, type_encoding::coordinates_xor);
  } catch(std::invalid_argument const&) {
    return decode_tiles(source, type_encoding::plain);
  }
}

std::vector<uint8_t> encode_theatre(theatre const &source) {
  /// Rebuild the original 64-tile CCT stream, preserving native position packing, byte angles and XOR type words
  std::vector<uint8_t> output;
  auto byte{[&](uint8_t const value){output.push_back(value);}};
  auto word{[&](uint16_t const value){
    byte(static_cast<uint8_t>(value));
    byte(static_cast<uint8_t>(value >> 8));
  }};
  auto dword{[&](uint32_t const value){
    word(static_cast<uint16_t>(value));
    word(static_cast<uint16_t>(value >> 16));
  }};
  for(unsigned int y{0}; y != theatre::rows; ++y) {
    for(unsigned int x{0}; x != theatre::columns; ++x) {
      std::vector<placed_object const*> placements;
      for(auto const &object : source.objects) {
        if(!object.has(object_flag::present) || object.pose == model_pose::editor_cursor) continue;
        int64_t const column{(static_cast<int64_t>(object.position.x) - theatre::origin) >> 21};
        int64_t const row{(static_cast<int64_t>(object.position.y) - theatre::origin) >> 21};
        if(column == x && row == y) placements.push_back(&object);
      }
      if(placements.size() > 65'535) throw std::invalid_argument{"native theatre tile has too many objects"};
      auto const &tile{source.tiles[y * theatre::columns + x]};
      dword(0x00656950u);
      word(static_cast<uint16_t>(x));
      word(static_cast<uint16_t>(y));
      word(static_cast<uint16_t>(placements.size()));
      byte(tile.storage_flags);
      for(auto const value : tile.packed_colours) byte(value);
      for(auto const *object : placements) {
        auto const low_x{static_cast<uint16_t>(object->position.x)};
        auto const low_y{static_cast<uint16_t>(object->position.y)};
        uint16_t const packed{static_cast<uint16_t>(((object->position.x >> 16) & 31) | (((object->position.y >> 16) & 31) << 5)
          | (((object->position.z >> 16) & 31) << 10))};
        word(packed);
        word(low_x);
        word(low_y);
        word(static_cast<uint16_t>(object->position.z));
        byte(static_cast<uint8_t>(object->attitude.heading >> 8));
        byte(static_cast<uint8_t>(object->attitude.pitch >> 8));
        byte(static_cast<uint8_t>(object->attitude.roll >> 8));
        word(object->flags);
        for(unsigned int axis{0}; axis != 3; ++axis) word(static_cast<uint16_t>(object->velocity[axis]));
        word(object->timer);
        word(static_cast<uint16_t>(std::to_underlying(object->type) ^ (source.expansion ? 0 : low_x ^ low_y)));
      }
    }
  }
  for(unsigned int axis{0}; axis != 3; ++axis) dword(static_cast<uint32_t>(source.start[axis]));
  return output;
}

void apply_briefing(theatre &theatre, std::span<uint8_t const> const source) {
  /// TWR's optional first-line X directive controls the actual sun, atmosphere and star settings
  theatre.missions.clear();
  if(source.empty()) {
    theatre.briefing.clear();
    return;
  }
  theatre.briefing.assign(reinterpret_cast<char const*>(source.data()), source.size());
  std::istringstream lines{theatre.briefing};
  std::string line;
  std::string title;
  while(std::getline(lines, line)) {
    if(!line.empty() && line.back() == '\r') line.pop_back();
    if(line.find("Mission Code Name") != std::string::npos) {
      auto const colon{line.find(':')};
      if(colon != std::string::npos) {
        title = line.substr(colon + 1);
        title.erase(0, title.find_first_not_of(' '));
      }
    }
    if(line.starts_with("Sxyth")) {
      std::istringstream record{line.substr(5)};
      mission_start start{
        .title{title.empty() ? "Mission " + std::to_string(theatre.missions.size() + 1) : title},
      };
      int32_t heading{0};
      if(!(record >> start.position.x >> start.position.y >> start.position.z >> heading)) throw std::invalid_argument{"invalid mission start directive"};
      start.heading = static_cast<maths::angle>(heading);
      theatre.missions.push_back(std::move(start));
    }
  }
  if(source.front() != 'X') return;
  std::istringstream input{theatre.briefing.substr(1, theatre.briefing.find('\n'))};
  int angle{0};
  int diameter{0};
  int colour{0};
  int gravity{0};
  int density{0};
  int stars{0};
  int height{0};
  if(!(input >> angle >> diameter >> colour >> gravity >> density >> stars)) throw std::invalid_argument{"embedded theatre has an invalid X directive"};
  // Older TWR headers omit atmosphere height; READUNIVPARAMS retains the engine default in that case.
  input >> height;
  theatre.sky = {
    .sun_angle{static_cast<maths::angle>(angle)},
    .first_sun_diameter{static_cast<uint16_t>(diameter)},
    .second_sun_diameter{static_cast<uint16_t>(diameter + 5)},
    .sun_colour{static_cast<uint8_t>(colour & 15)},
    .stars{(stars & 1) != 0},
  };
  theatre.environment.gravity = static_cast<int16_t>(std::clamp(gravity, -100, -15));
  if(density >= 1000 && density <= 10'000) theatre.environment.sea_level_density = static_cast<uint16_t>(density);
  if(height >= 256 && height <= 32'767) theatre.environment.atmosphere_height = static_cast<uint16_t>(height);
  theatre.briefing.erase(0, theatre.briefing.find('\n') + 1);
}

} // namespace corncob::world

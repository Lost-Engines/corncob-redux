#include "airfield_survey.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <format>
#include <sstream>
#include <tuple>
#include "maths/integer.h"
#include "resources/survey_types.h"
#include "world/airfields.h"

namespace corncob::resources {

namespace {

uint8_t constexpr group_mask{31}, objective_marker{64};
int32_t constexpr grouping_distance{400'000};
bool nearby(survey_entry const &a, survey_entry const &b) {
  /// MOAG groups by a strict square in world coordinates, independently of the displayed range
  return std::abs(static_cast<int64_t>(a.position.x) - b.position.x) < grouping_distance
    && std::abs(static_cast<int64_t>(a.position.y) - b.position.y) < grouping_distance;
}
auto order(survey_entry const &entry) {
  /// Preserve the original six comparison keys; completed objectives precede incomplete ones within a group
  return std::tuple{entry.group & group_mask, survey_types[std::to_underlying(entry.type)].attributes & 2u,
    entry.group & objective_marker, (entry.group & objective_marker) != 0 ? -(entry.flags & 0x200) : 0,
    std::to_underlying(entry.type), entry.flags & 0x7000};
}
void sort_entries(std::span<survey_entry> entries) {
  /// Borland's median partition gives equal-key objects a visible order; a different qsort would reorder their range lines
  while(entries.size() > 1) {
    unsigned int const count{static_cast<unsigned int>(entries.size())};
    if(count == 2) {
      if(order(entries[0]) > order(entries[1])) std::swap(entries[0], entries[1]);
      return;
    }
    unsigned int last{count - 1};
    unsigned int middle{count / 2};
    if(order(entries[middle]) > order(entries[last])) std::swap(entries[middle], entries[last]);
    if(order(entries[middle]) > order(entries[0])) {
      std::swap(entries[middle], entries[0]);
    } else if(order(entries[0]) > order(entries[last])) std::swap(entries[0], entries[last]);
    if(count == 3) {
      std::swap(entries[0], entries[middle]);
      return;
    }
    unsigned int equal_end{1};
    unsigned int scan{1};
    for(;;) {
      auto const comparison{order(entries[scan]) <=> order(entries[0])};
      if(comparison <= 0) {
        if(comparison == 0) std::swap(entries[scan], entries[equal_end++]);
        if(scan >= last) break;
        ++scan;
      } else {
        while(scan < last) {
          auto const tail{order(entries[0]) <=> order(entries[last])};
          if(tail < 0) {
            --last;
          } else {
            std::swap(entries[scan], entries[last]);
            if(tail != 0) {
              ++scan;
              --last;
            }
            break;
          }
        }
        if(scan >= last) break;
      }
    }
    if(order(entries[scan]) <= order(entries[0])) ++scan;
    unsigned int equal{0};
    unsigned int small{scan - 1};
    while(equal < equal_end && small >= equal_end) std::swap(entries[equal++], entries[small--]);
    unsigned int const left_count{scan - equal_end};
    unsigned int const right_count{count - scan};
    if(right_count < left_count) {
      sort_entries(entries.subspan(scan));
      entries = entries.first(left_count);
    } else {
      sort_entries(entries.first(left_count));
      entries = entries.subspan(scan);
    }
  }
}
std::string bearing(int32_t const x, int32_t const y) {
  /// The game's positive X axis is north and positive Y is west; MOAG uses integer ratio boundaries of 1.50 and 5.03
  if(x == 0 && y == 0) return "na";
  char const north_south{x >= 0 ? 'N' : 'S'};
  char const east_west{y >= 0 ? 'W' : 'E'};
  int64_t const a{std::abs(static_cast<int64_t>(x))};
  int64_t const b{std::abs(static_cast<int64_t>(y))};
  if(b == 0) return std::string(1, north_south);
  if(a == 0) return std::string(1, east_west);
  if(a > b) {
    auto const ratio{a * 100 / b};
    if(ratio > 503) return std::string(1, north_south);
    if(ratio > 150) return std::string(2, north_south) + east_west;
  } else {
    auto const ratio{b * 100 / a};
    if(ratio > 503) return std::string(1, east_west);
    if(ratio > 150) return std::string(1, east_west) + north_south + east_west;
  }
  return std::string(1, north_south) + east_west;
}
std::string distance(int32_t x, int32_t y) {
  /// Quantise each axis into tenths of a mile before applying the original 100-iteration integer Newton square root
  int32_t constexpr centimetres_per_tenth_mile{16'090};
  x /= centimetres_per_tenth_mile;
  y /= centimetres_per_tenth_mile;
  int16_t result{maths::word(std::abs(x) + std::abs(y))};
  int32_t const square{maths::dword(static_cast<int64_t>(x) * x + static_cast<int64_t>(y) * y)};
  if(square != 0) {
    for(unsigned int iteration{0}; iteration != 100; ++iteration) {
      int16_t const previous{result};
      result = static_cast<int16_t>(maths::word(result + square / result) / 2);
      if(result == previous) break;
    }
  }
  auto text{std::to_string(result)};
  text.insert(text.end() - 1, '.');
  return text;
}

} // anonymous namespace

airfield_status survey_airfield(world::theatre const &theatre, unsigned int const airfield) {
  auto const origin{world::multiple_airfields(theatre) && airfield < world::airfield_count ? world::airfield_start(theatre, airfield) : maths::world_position{theatre.start}};
  /// INT counts every incomplete objective in a single-airfield theater and every unwrecked plane within its start box
  int32_t constexpr boarding_region{524'288};
  int32_t constexpr pilot_height{720};
  airfield_status result;
  for(auto const &object : theatre.objects) {
    if(!object.has(world::object_flag::present) || (airfield < world::airfield_count && !world::belongs_to_airfield(theatre, object, airfield))) continue;
    using enum world::object_type;
    if(object.type == aircraft && (object.flags & 0x100) == 0
      && std::abs(static_cast<int64_t>(object.position.x) - origin.x) < boarding_region
      && std::abs(static_cast<int64_t>(object.position.y) - origin.y) < boarding_region
      && std::abs(static_cast<int64_t>(object.position.z) - pilot_height) < boarding_region) ++result.planes;
    if(!object.has(world::object_flag::objective_complete)
      && (object.type == generator_orb || object.type == destroyed_generator_orb || object.type == objective_saucer
        || (object.type == ground_crew && object.has(world::object_flag::special))
        || (object.type == portal && (object.quality() == 4 || object.quality() == 7)))) ++result.objectives;
  }
  return result;
}

void group_survey_entries(std::span<survey_entry> const entries) {
  /// The first tower anchors friendlies; mission objectives claim ungrouped neighbours; all other objects share a final group
  uint8_t next{1};
  for(auto &anchor : entries) {
    if(anchor.type != world::object_type::control_tower) continue;
    uint8_t const group{next++};
    anchor.group |= group;
    for(auto &entry : entries) {
      if((survey_types[std::to_underlying(entry.type)].attributes & 2u) == 0 && nearby(anchor, entry)) entry.group |= group;
    }
    break;
  }
  for(auto &anchor : entries) {
    using enum world::object_type;
    bool const objective{anchor.type == generator_orb || anchor.type == destroyed_generator_orb || anchor.type == objective_saucer
      || (anchor.type == portal && ((anchor.flags & 0x7000) == 0x4000 || (anchor.flags & 0x7000) == 0x7000))};
    if(!objective) continue;
    anchor.group |= objective_marker;
    if((anchor.group & group_mask) == 0) anchor.group |= next++;
    for(auto &entry : entries) {
      if((entry.group & group_mask) == 0 && nearby(anchor, entry)) entry.group |= anchor.group & group_mask;
    }
  }
  for(auto &entry : entries) {
    if((entry.group & group_mask) == 0) entry.group |= next;
  }
  sort_entries(entries);
}
std::string describe_survey_entry(survey_entry const &entry, maths::world_position const &origin) {
  /// These labels, overloaded flag meanings and field widths are the original 17-byte MOAG survey record formatter
  auto const &definition{survey_types.at(std::to_underlying(entry.type))};
  if((definition.attributes & 1u) == 0) return {};
  auto name{std::string{(definition.attributes & 2u) != 0 ? "Enemy " : ""} + std::string{definition.name}};
  unsigned int const quality{(entry.flags >> 12) & 7u};
  bool typed{false};
  using enum world::object_type;
  switch(entry.type) {
  case generator_orb:
    if(quality == 4) {
      name += " (Indestructible)";
    } else if((entry.flags & 0x800) != 0) name += " (Gravity)";
    break;
  case aircraft:
    name += std::format(" ({})", quality == 0 ? "Corsair" : quality == 1 ? "Dumpy" : quality == 2 ? "Stealth" : "Unknown type");
    if((entry.flags & 0x100) != 0) name += " (Wrecked)";
    break;
  case ground_crew:
    if((entry.flags & 0x800) != 0) name += " (Spy)";
    if(quality == 1) name += " (Dead)";
    break;
  case ground_vehicle:
    typed = true;
    if((entry.flags & 0xff) == 0x71) {
      name += " (Dead)";
    } else if((entry.flags & 0x800) != 0) name += " (bumpers)";
    break;
  case crater:
    if(quality != 0) return {};
    break;
  case shell:
    {
    std::array<std::string_view, 8> constexpr types{"vehicle site", "mortar", "KLA site", "AAA site", "barrage balloon", "bee-swarm", "fuel dump", "unknown type"};
    name += std::format(" ({})", types[quality]);
  }
    break;
  case kla_site:
  case vehicle_site:
  case aaa_site:
  case kla:
  case saucer:
  case saucer_site:
  case objective_saucer:
  case portal:
    typed = true;
    break;
  default:
    break;
  }
  std::string result{typed ? std::format("{:<27.27} Type \\{}   ", name, quality + 1) : std::format("{:<36.36} ", name)};
  int32_t const dx{maths::dword(static_cast<int64_t>(entry.position.x) - origin.x)};
  int32_t const dy{maths::dword(static_cast<int64_t>(entry.position.y) - origin.y)};
  int32_t const altitude{maths::dword(static_cast<int64_t>(entry.position.z) * 10) / 305};
  result += std::format("{:>4.4} {:>3}    {:>5}             ({})\r\n", distance(dx, dy), bearing(dx, dy), altitude, entry.group & group_mask);
  std::string_view objective;
  if(entry.type == generator_orb || entry.type == destroyed_generator_orb) {
    std::array<std::string_view, 8> constexpr descriptions{"destroy this orb.", "blow up the nearby TRFRU/fuel dumps.", "eliminate the nearby KLA and AAA.", "kill all nearby alien ground vehicles.", "destroy this orb with a suicide plane.", "blast the nearby saucer sites.", "destroy all nearby alien mind-benders.", "kill the nearby congregation of aliens."};
    objective = descriptions[quality];
  } else if(entry.type == portal && (quality == 4 || quality == 7)) {
    objective = "Fly through this portal.";
  } else if(entry.type == objective_saucer) {
    objective = "Blast this saucer out of the sky.";
  } else if(entry.type == ground_crew && (entry.flags & 0x800) != 0) objective = "Get secret document from spy.";
  if(!objective.empty() && (entry.flags & 0x200) == 0) result += std::format("\\(         Mission Objective:  {}\\)\r\n", objective);
  return result;
}
std::vector<std::string> airfield_survey(world::theatre const &theatre, unsigned int const airfield) {
  /// A theater with one tower reports all 64 tiles, including distant mission objectives
  std::vector<survey_entry> entries;
  for(auto const &object : theatre.objects) {
    if(!world::belongs_to_airfield(theatre, object, airfield)) continue;
    if(!object.has(world::object_flag::present) || (survey_types[std::to_underlying(object.type)].attributes & 1u) == 0) continue;
    entries.push_back({
      .position{maths::world_position{object.position}},
      .type{object.type},
      .flags{object.flags},
    });
  }
  group_survey_entries(entries);
  std::string text{"\\(Objects near " + std::string{world::airfield_names[airfield]} + ":\\)\r\n\r\n\\(Object                    Quality:  Range/Dir:    Alt:\\)\r\n\\(                                     (Mi.)       (ft.)\\)\r\n"};
  unsigned int previous{0};
  for(auto const &entry : entries) {
    if((entry.group & group_mask) != previous) text += "\r\n\r\n";
    text += describe_survey_entry(entry, world::multiple_airfields(theatre) ? world::airfield_start(theatre, airfield) : theatre.start);
    previous = entry.group & group_mask;
  }
  text += "\r\n\r\n";
  std::vector<std::string> result;
  std::istringstream input{text};
  std::string line;
  while(std::getline(input, line)) {
    if(!line.empty() && line.back() == '\r') line.pop_back();
    result.push_back(line);
  }
  return result;
}

} // namespace corncob::resources

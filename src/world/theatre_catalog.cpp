#include "theatre_catalog.h"
#include <algorithm>
#include <stdexcept>
#include <utility>
#include "resources/assets.h"
#include "resources/collection.h"

namespace corncob::world {

namespace {

resources::collection const &expansion() {
  /// Embedded resource spans remain valid for the lifetime of every campaign and flight.
  static resources::collection const assets{resources::other_worlds_asset()};
  return assets;
}

} // anonymous namespace

std::span<theatre_definition const> theatres(theatre_group const group) noexcept {
  /// Grouping affects selection only; every world retains its own edition and original definition stem.
  switch(group) {
  case theatre_group::earth: return theatre_catalog;
  case theatre_group::other_worlds: return other_worlds_catalog;
  case theatre_group::practice: return practice_catalog;
  }
  std::unreachable();
}
std::span<theatre_definition const> campaign_theatres(campaign_id const campaign) noexcept {
  static auto constexpr expansion{[]{
    std::array<theatre_definition, other_worlds_catalog.size() + practice_catalog.size()> result{};
    unsigned int index{0};
    for(auto const &definition : other_worlds_catalog) result[index++] = definition;
    for(auto const &definition : practice_catalog) result[index++] = definition;
    return result;
  }()};
  return campaign == campaign_id::earth ? std::span<theatre_definition const>{theatre_catalog} : std::span<theatre_definition const>{expansion};
}
theatre_definition const &find_theatre(std::string_view const name) {
  /// Earth keeps its historical command-line names; expansion variants are explicitly namespaced.
  for(auto const group : {theatre_group::earth, theatre_group::other_worlds, theatre_group::practice}) {
    for(auto const &definition : theatres(group)) if(definition.name == name) return definition;
  }
  throw std::invalid_argument{"unknown theatre '" + std::string{name} + "'; use --list-theatres to list the available worlds"};
}
theatre_group group_for(std::string_view const name) {
  /// Practice definitions never become an additional campaign completion requirement.
  for(auto const group : {theatre_group::earth, theatre_group::other_worlds, theatre_group::practice}) {
    for(auto const &definition : theatres(group)) if(definition.name == name) return group;
  }
  find_theatre(name);
  std::unreachable();
}
std::span<uint8_t const> theatre_briefing(std::string_view const name) {
  /// Some editor and recorded-flight example universes have no individual tower file.
  auto const &definition{find_theatre(name)};
  if(definition.campaign == campaign_id::other_worlds) {
    std::string const stem{definition.dos_name};
    if(stem == "default" || stem == "demo1") return expansion().file("def/deftower.twr");
    return expansion().file("def/" + stem + ".twr");
  }
  if(name == "morning") return resources::morning_briefing_asset();
  if(name == "dusk") return resources::dusk_briefing_asset();
  if(name == "wasteland") return resources::wasteland_briefing_asset();
  return resources::white_sands_briefing_asset();
}
theatre load_theatre(std::string_view const name) {
  /// The same checked decoder accepts both original CCT XOR records and the expansion's plain DEF records.
  auto const &definition{find_theatre(name)};
  std::span<uint8_t const> bytes;
  if(definition.campaign == campaign_id::other_worlds) bytes = expansion().file("def/" + std::string{definition.dos_name} + ".def");
  else if(name == "morning") bytes = resources::morning_asset();
  else if(name == "dusk") bytes = resources::dusk_asset();
  else if(name == "wasteland") bytes = resources::wasteland_asset();
  else bytes = resources::white_sands_asset();
  auto result{decode_theatre(bytes)};
  result.expansion = definition.campaign == campaign_id::other_worlds;
  apply_briefing(result, theatre_briefing(name));
  return result;
}

} // namespace corncob::world

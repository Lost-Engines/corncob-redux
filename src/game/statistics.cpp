#include "statistics.h"
#include <stdexcept>
#include <utility>
#include "maths/integer.h"
#include "resources/binary_reader.h"

namespace corncob::game {

bool sortie_statistics::has(sortie_status const flag) const noexcept {
  /// Preserve priority-based outcome tests without conflating present and historical sortie state
  return (status & std::to_underlying(flag)) != 0;
}
bool sortie_statistics::counts_for_career() const noexcept {
  /// MOAG's invulnerable and Control-C result branches return before career accumulation and awards
  return !has(sortie_status::invincible) && !has(sortie_status::aborted);
}
void sortie_statistics::mark(sortie_status const flag) noexcept {
  /// A sortie can retain several historical flags; scoring decides their original priority
  status |= std::to_underlying(flag);
}
std::array<uint8_t, sortie_statistics::native_bytes> sortie_statistics::encode() const {
  /// Serialize the original result block explicitly, independent of C++ padding and endian order
  std::array<uint8_t, native_bytes> result{};
  unsigned int offset{0};
  auto word{[&](uint16_t const value){
    result[offset++] = static_cast<uint8_t>(value);
    result[offset++] = static_cast<uint8_t>(value >> 8);
  }};
  for(auto const value : {status, contacting_type, aircraft_lost, resource_kills}) word(value);
  for(auto const *counts : {&kla_kills, &aaa_kills, &vehicle_kills, &site_kills}) for(auto const value : *counts) word(value);
  for(auto const value : {objectives_completed, friendly_destroyed, maximum_damage}) word(value);
  for(auto const *counts : {&peak_aaa, &peak_kla}) for(auto const value : *counts) word(value);
  word(static_cast<uint16_t>(elapsed_seconds));
  word(static_cast<uint16_t>(elapsed_seconds >> 16));
  for(auto const value : orb_kills) word(value);
  return result;
}
sortie_statistics sortie_statistics::decode(std::span<uint8_t const> const source) {
  /// Retain every native statistics word; MOAG accumulates the flight engine's elapsed word as a dword
  if(source.size() != native_bytes) throw std::invalid_argument{"invalid native sortie statistics length"};
  resources::binary_reader const input{source};
  unsigned int offset{0};
  auto word{[&]{auto const value{input.word(offset)}; offset += 2; return value;}};
  sortie_statistics result;
  result.status = word();
  result.contacting_type = word();
  result.aircraft_lost = word();
  result.resource_kills = word();
  for(auto *counts : {&result.kla_kills, &result.aaa_kills, &result.vehicle_kills, &result.site_kills}) for(auto &value : *counts) value = word();
  result.objectives_completed = word();
  result.friendly_destroyed = word();
  result.maximum_damage = word();
  for(auto *counts : {&result.peak_aaa, &result.peak_kla}) for(auto &value : *counts) value = word();
  uint32_t const elapsed_low{word()}, elapsed_high{word()};
  result.elapsed_seconds = elapsed_low | (elapsed_high << 16);
  for(auto &value : result.orb_kills) value = word();
  return result;
}
score_factors calculate_score_factors(sortie_statistics const &statistics, score_context const context) {
  /// ISCORE retains signed word intermediates and wrapping dword multiplication before its final signed division
  if(statistics.has(sortie_status::aborted)) return {};
  bool const campaign{context == score_context::campaign};
  bool const suicide{campaign && statistics.has(sortie_status::suicide_objective)};
  // MOAG adds successful suicide-objective credit and contains an always-false TEST ...,0 in its ejection test
  int const present_factor{statistics.has(sortie_status::killed) ? 2 : suicide && statistics.has(sortie_status::rescued) ? 5
    : statistics.has(sortie_status::captured) ? 1 : statistics.has(sortie_status::home) ? 5
    : statistics.has(sortie_status::rescued) ? 4 : suicide ? 2 : 1};
  int const sortie_factor{statistics.has(sortie_status::crashed_away) || (!campaign && statistics.has(sortie_status::ejected)) ? 1
    : statistics.has(sortie_status::crashed_home) ? 3 : statistics.has(sortie_status::landed) || suicide ? 5 : 1};
  int32_t primary{maths::word(statistics.objectives_completed * 15u + statistics.resource_kills)};
  int16_t danger{10};
  for(unsigned int q{0}; q != 8; ++q) {
    primary = maths::dword(static_cast<int64_t>(primary) + maths::word(statistics.aaa_kills[q] + statistics.site_kills[q] + statistics.orb_kills[q]));
    danger = maths::word(danger + (q + 1) * (statistics.peak_aaa[q] + statistics.peak_kla[q]));
  }
  int32_t score{maths::dword(static_cast<int64_t>(maths::dword(static_cast<int64_t>(primary) * danger)) * 5) / 10};
  for(unsigned int q{0}; q != 8; ++q) score = maths::dword(static_cast<int64_t>(score)
    + maths::word((q + 1) * (statistics.kla_kills[q] + statistics.vehicle_kills[q])));
  return {
    .present{present_factor},
    .mission{sortie_factor},
    .base{score},
  };
}
int32_t calculate_score(sortie_statistics const &statistics, score_context const context) {
  /// Share ISCORE's displayed PMSF, MSF and MOBF with its unchanged wrapping score calculation
  if(statistics.has(sortie_status::aborted)) return 0;
  auto const factors{calculate_score_factors(statistics, context)};
  int32_t const score{maths::dword(static_cast<int64_t>(factors.base) * factors.present * factors.mission)};
  int16_t const losses{maths::word(statistics.aircraft_lost + 1)};
  if(losses == 0) throw std::invalid_argument{"native sortie score has an invalid aircraft-loss divisor"};
  return maths::dword(static_cast<int64_t>(score) / losses);
}

} // namespace corncob::game

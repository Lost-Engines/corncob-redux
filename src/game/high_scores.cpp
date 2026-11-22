#include "high_scores.h"
#include <algorithm>
#include <stdexcept>
#include "maths/integer.h"
#include "resources/binary_reader.h"

namespace corncob::game {

namespace {

void write_name(score_name &field, std::string_view const text) {
  /// Native strcpy terminates a new name but leaves the previous bytes after that terminator intact
  auto const length{std::min(text.size(), field.size() - 1)};
  std::copy_n(text.begin(), length, field.begin());
  field[length] = 0;
}
void write_theatre(mission_score &entry, std::string_view const text) {
  /// The original White Sands title exceeds its 22-byte name field and overwrites the first statistics bytes; reproduce that within bounds
  auto statistics{entry.statistics.encode()};
  auto const length{std::min(text.size(), entry.theatre.size() + statistics.size() - 1)};
  for(unsigned int i{0}; i <= length; ++i) {
    uint8_t const byte{i == length ? uint8_t{0} : static_cast<uint8_t>(text[i])};
    if(i < entry.theatre.size()) entry.theatre[i] = byte;
    else statistics[i - entry.theatre.size()] = byte;
  }
  entry.statistics = sortie_statistics::decode(statistics);
}
int16_t ground_kills(sortie_statistics const &value) {
  /// MOAG's displayed ground sum includes AAA, launch sites and generators, with its original word wrapping
  int32_t sum{value.resource_kills + value.objectives_completed};
  for(auto const *counts : {&value.aaa_kills, &value.site_kills, &value.orb_kills}) for(auto const count : *counts) sum += count;
  return maths::word(sum);
}
int16_t air_kills(sortie_statistics const &value) {
  /// The native air total combines KLA and the vehicle counter, regardless of the latter category's name
  int32_t sum{0};
  for(auto const *counts : {&value.kla_kills, &value.vehicle_kills}) for(auto const count : *counts) sum += count;
  return maths::word(sum);
}
int compare_statistics(sortie_statistics const &a, int32_t const a_points, sortie_statistics const &b, int32_t const b_points) {
  /// Equal native point totals are resolved by signed objective, ground-kill and air-kill words, in that order
  if(a_points != b_points) return a_points > b_points ? 1 : -1;
  for(auto const &pair : {std::pair{maths::word(a.objectives_completed), maths::word(b.objectives_completed)},
                        std::pair{ground_kills(a), ground_kills(b)}, std::pair{air_kills(a), air_kills(b)}}) {
    if(pair.first != pair.second) return pair.first > pair.second ? 1 : -1;
  }
  return 0;
}
int compare_rate(career_score const &a, career_score const &b) {
  /// The original uses signed mission words, wrapping subtraction of truncated dword rates, then total points
  int16_t const a_count{maths::word(a.sorties)}, b_count{maths::word(b.sorties)};
  if(a_count == 0) return b_count == 0 ? 0 : -1;
  if(b_count == 0) return 1;
  int32_t const a_rate{maths::dword(static_cast<int64_t>(a.points) / a_count)}, b_rate{maths::dword(static_cast<int64_t>(b.points) / b_count)};
  auto const difference{maths::dword(static_cast<int64_t>(a_rate) - b_rate)};
  if(difference != 0) return difference > 0 ? 1 : -1;
  return a.points == b.points ? 0 : a.points > b.points ? 1 : -1;
}
template<typename Entry, typename Better>
std::optional<unsigned int> insert_position(std::array<Entry, 10> &table, Better better, std::optional<std::string_view> const career = std::nullopt) {
  /// Update the first live matching career, otherwise fill the first empty row or displace a strictly lower last entry
  unsigned int slot{static_cast<unsigned int>(table.size())};
  if(career) for(unsigned int i{0}; i != table.size(); ++i) {
    if(table[i].name.front() != 0 && table[i].retired == 0 && score_text(table[i].name) == *career) { slot = i; break; }
  }
  if(slot == table.size()) {
    for(unsigned int i{0}; i != table.size(); ++i) if(table[i].name.front() == 0) { slot = i; break; }
    if(slot == table.size()) {
      if(!better(table.back())) return std::nullopt;
      --slot;
    }
  }
  // the original only bubbles upward, even if an existing career's average has declined
  while(slot != 0 && better(table[slot - 1])) {
    table[slot] = table[slot - 1];
    --slot;
  }
  return slot;
}
void update_career(std::array<career_score, 10> &table, career_score const &candidate, bool const fastest) {
  /// Retain original tie rules and old string padding rather than rebuilding or globally sorting the table
  auto better{[&candidate, fastest](career_score const &entry){
    return (fastest ? compare_rate(candidate, entry) : compare_statistics(candidate.statistics, candidate.points, entry.statistics, entry.points)) > 0;
  }};
  if(auto const slot{insert_position(table, better, score_text(candidate.name))}) {
    auto &entry{table[*slot]};
    write_name(entry.name, score_text(candidate.name));
    entry.sorties = candidate.sorties;
    entry.resurrections = candidate.resurrections;
    entry.statistics = candidate.statistics;
    entry.points = candidate.points;
    entry.retired = 0;
  }
}

} // anonymous namespace

std::string score_text(score_name const &value) {
  /// Display up to the first NUL without discarding opaque padding from saved entries
  auto const end{std::ranges::find(value, 0)};
  return {reinterpret_cast<char const*>(value.data()), static_cast<size_t>(end - value.begin())};
}
score_tables score_tables::decode(std::span<uint8_t const> const source) {
  /// PILOT.HSC has ten 180-byte mission records followed by two sets of ten 162-byte career records
  if(source.size() != native_bytes) throw std::invalid_argument{"invalid native high-score table length"};
  resources::binary_reader const data{source};
  score_tables result;
  unsigned int offset{0};
  auto name{[&](score_name &field){std::copy_n(source.begin() + offset, field.size(), field.begin()); offset += static_cast<unsigned int>(field.size());}};
  auto statistics{[&](sortie_statistics &field){field = sortie_statistics::decode(source.subspan(offset, sortie_statistics::native_bytes)); offset += sortie_statistics::native_bytes;}};
  for(auto &entry : result.missions) {
    name(entry.name);
    name(entry.theatre);
    statistics(entry.statistics);
    entry.points = data.signed_dword(offset);
    entry.retired = data.word(offset + 4);
    offset += 6;
  }
  for(auto *table : {&result.careers, &result.fastest}) for(auto &entry : *table) {
    name(entry.name);
    entry.sorties = data.word(offset);
    entry.resurrections = data.word(offset + 2);
    offset += 4;
    statistics(entry.statistics);
    entry.points = data.signed_dword(offset);
    entry.retired = data.word(offset + 4);
    offset += 6;
  }
  return result;
}
std::array<uint8_t, score_tables::native_bytes> score_tables::encode() const {
  /// Encode the exact original record sizes and preserve every fixed-width name byte and result field
  std::array<uint8_t, native_bytes> result{};
  unsigned int offset{0};
  auto word{[&](uint16_t const value){result[offset++] = static_cast<uint8_t>(value); result[offset++] = static_cast<uint8_t>(value >> 8);}};
  auto tail{[&](int32_t const points, uint16_t const retired){word(static_cast<uint16_t>(points)); word(static_cast<uint16_t>(static_cast<uint32_t>(points) >> 16)); word(retired);}};
  auto name{[&](score_name const &value){std::ranges::copy(value, result.begin() + offset); offset += static_cast<unsigned int>(value.size());}};
  auto statistics{[&](sortie_statistics const &value){auto const raw{value.encode()}; std::ranges::copy(raw, result.begin() + offset); offset += static_cast<unsigned int>(raw.size());}};
  for(auto const &entry : missions) {
    name(entry.name);
    name(entry.theatre);
    statistics(entry.statistics);
    tail(entry.points, entry.retired);
  }
  for(auto const *table : {&careers, &fastest}) for(auto const &entry : *table) {
    name(entry.name);
    word(entry.sorties);
    word(entry.resurrections);
    statistics(entry.statistics);
    tail(entry.points, entry.retired);
  }
  return result;
}
void score_tables::rename(std::string_view const previous, std::string_view const name) {
  /// Live achievements follow a renamed career; retired careers with a reused name remain distinct
  auto replace{[name](score_name &field){
    unsigned int const length{static_cast<unsigned int>(std::min(name.size(), field.size() - 1))};
    std::copy_n(name.begin(), length, field.begin());
    field[length] = 0;
  }};
  for(auto &entry : missions) if(entry.retired == 0 && score_text(entry.name) == previous) replace(entry.name);
  for(auto *table : {&careers, &fastest}) for(auto &entry : *table) {
    if(entry.retired == 0 && score_text(entry.name) == previous) replace(entry.name);
  }
}
void score_tables::clear(unsigned int const list) {
  /// Each of the original three score lists can be reset independently
  if(list == 0) missions = {};
  else if(list == 1) careers = {};
  else if(list == 2) fastest = {};
  else throw std::invalid_argument{"unknown high-score list"};
}
void score_tables::retire(std::string_view const name) {
  /// Preserve a deleted pilot's historical results while allowing a later pilot to reuse the same name
  for(auto &entry : missions) if(score_text(entry.name) == name) entry.retired = 1;
  for(auto *table : {&careers, &fastest}) for(auto &entry : *table) if(score_text(entry.name) == name) entry.retired = 1;
}
void score_tables::record(std::string_view const theatre, sortie_statistics const &result, pilot_record const *const pilot) {
  /// Mission achievements and live career rows use MOAG's insertion order, signed counters and independent tie breakers
  if(!result.counts_for_career()) return;
  std::string const name{pilot ? pilot->name() : "trainee"};
  int32_t const points{pilot ? pilot->last_sortie_score() : calculate_score(result, score_context::campaign)};
  auto better{[&result, points](mission_score const &entry){
    return compare_statistics(result, points, entry.statistics, entry.points) > 0;
  }};
  if(auto const slot{insert_position(missions, better)}) {
    auto &entry{missions[*slot]};
    entry.statistics = result;
    entry.points = points;
    entry.retired = 0;
    write_name(entry.name, name);
    write_theatre(entry, theatre);
  }
  if(!pilot) return;
  career_score candidate{
    .sorties{pilot->sorties()},
    .resurrections{pilot->resurrections()},
    .statistics{pilot->career()},
    .points{pilot->career_score()},
  };
  write_name(candidate.name, name);
  update_career(careers, candidate, false);
  if(maths::word(candidate.sorties) >= 3) update_career(fastest, candidate, true);
}

std::span<mission_score const> score_tables::best_missions() const noexcept {
  /// Expose ranked mission records without allowing the interface to reorder saved entries
  return missions;
}
std::span<career_score const> score_tables::best_careers() const noexcept {
  /// Expose cumulative points alongside their original career statistics
  return careers;
}
std::span<career_score const> score_tables::fastest_careers() const noexcept {
  /// The table stores full points/counters; display can report the integer rate without losing the original totals
  return fastest;
}

} // namespace corncob::game

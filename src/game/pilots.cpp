#include "pilots.h"
#include <algorithm>
#include <bit>
#include <stdexcept>
#include <utility>
#include "maths/integer.h"
#include "resources/binary_reader.h"

namespace corncob::game {

namespace {

unsigned int constexpr latest_statistics_offset{28}, latest_score_offset{158};
unsigned int constexpr best_statistics_offset{162}, peak_statistics_offset{296};
unsigned int constexpr statistics_and_score_bytes{sortie_statistics::native_bytes + 4};
unsigned int constexpr career_statistics_offset{430}, career_score_offset{560};
unsigned int constexpr rank_offset{598}, flags_offset{22}, sorties_offset{24}, resurrection_offset{26};
unsigned int constexpr maximum_name_length{20};

} // anonymous namespace

uint16_t pilot_record::word(unsigned int const offset) const {
  /// Preserve unlabelled native record bytes while exposing verified fields through typed accessors
  return resources::binary_reader{data}.word(offset);
}
void pilot_record::word(unsigned int const offset, uint16_t const value) {
  /// Native fields are little-endian regardless of host layout
  data.at(offset) = static_cast<uint8_t>(value);
  data.at(offset + 1) = static_cast<uint8_t>(value >> 8);
}
int32_t pilot_record::dword(unsigned int const offset) const {
  /// Scores retain signed original dword representation
  return resources::binary_reader{data}.signed_dword(offset);
}
void pilot_record::dword(unsigned int const offset, int32_t const value) {
  /// Write wrapping scores without depending on object representation
  word(offset, static_cast<uint16_t>(value));
  word(offset + 2, static_cast<uint16_t>(static_cast<uint32_t>(value) >> 16));
}
pilot_record::pilot_record(std::string_view const name) {
  /// New native pilots begin at second lieutenant with empty statistics and medals
  rename(name);
  data[0x234] = 5;
  data[0x235] = 6;
  data[0x236] = 2;
  data[0x239] = 4;
  data[0x23b] = 2;
  data[0x23d] = 128;
}
pilot_record::pilot_record(std::span<uint8_t const> const source) {
  /// Imported records retain every byte, including timestamps and fields not yet independently labelled
  if(source.size() != data.size()) throw std::invalid_argument{"invalid native pilot record length"};
  std::ranges::copy(source, data.begin());
}
std::string pilot_record::name() const {
  /// A record reserves twenty-two bytes although the original name prompt permits twenty characters
  auto const end{std::find(data.begin(), data.begin() + 22, 0)};
  return {reinterpret_cast<char const*>(data.data()), static_cast<size_t>(end - data.begin())};
}
void pilot_record::rename(std::string_view const name) {
  /// Keep DOS-compatible names bounded and terminated inside their original field
  if(name.empty() || name.size() > maximum_name_length || name.find_first_of("\r\n\t") != std::string_view::npos || name.find('\0') != std::string_view::npos) {
    throw std::invalid_argument{"pilot names must contain 1 to 20 characters"};
  }
  std::copy(name.begin(), name.end(), data.begin());
  data[name.size()] = 0;
}
bool pilot_record::selected() const {
  /// The front-end selection marker is independent of survival and rank
  return (word(flags_offset) & 0x8000u) != 0;
}
void pilot_record::select(bool const value) {
  /// Change only the original selected-record bit
  word(flags_offset, static_cast<uint16_t>(value ? word(flags_offset) | 0x8000u : word(flags_offset) & ~0x8000u));
}
std::span<uint8_t const> pilot_record::theatres() const {
  /// The native record stores twenty numbered-theatre references after its count and active-index bytes
  unsigned int constexpr maximum_theatres{20}, count_offset{0x23c}, first_theatre_offset{0x23e};
  if(data[count_offset] > maximum_theatres) throw std::invalid_argument{"pilot has too many saved-theatre references"};
  return std::span{data}.subspan(first_theatre_offset, data[count_offset]);
}
void pilot_record::add_theatre(uint8_t const number) {
  /// Save the original file number and make its zero-based list position active
  unsigned int constexpr maximum_theatres{20}, count_offset{0x23c}, active_offset{0x23d}, first_theatre_offset{0x23e};
  if(number == 0) throw std::invalid_argument{"saved-theatre number must be nonzero"};
  auto const entries{theatres()};
  auto const existing{std::ranges::find(entries, number)};
  if(existing != entries.end()) {
    data[active_offset] = static_cast<uint8_t>(existing - entries.begin());
    return;
  }
  if(entries.size() == maximum_theatres) throw std::invalid_argument{"pilot already has twenty saved theatres"};
  data[first_theatre_offset + entries.size()] = number;
  data[active_offset] = data[count_offset]++;
}
void pilot_record::remove_theatre(uint8_t const number) {
  /// Remove a numbered world while retaining the identity of the active world after compaction
  auto const entries{theatres()};
  auto const found{std::ranges::find(entries, number)};
  if(found == entries.end()) return;
  unsigned int const index{static_cast<unsigned int>(found - entries.begin())};
  auto const active{active_theatre()};
  std::move(data.begin() + 0x23e + index + 1, data.begin() + 0x23e + entries.size(), data.begin() + 0x23e + index);
  data[0x23e + --data[0x23c]] = 0;
  if(active) data[0x23d] = *active == index ? 128 : static_cast<uint8_t>(*active - (*active > index));
}
std::optional<unsigned int> pilot_record::active_theatre() const {
  /// Bit seven denotes training rather than an index into the twenty saved-theatre references
  return data[0x23d] < theatres().size() ? std::optional<unsigned int>{data[0x23d]} : std::nullopt;
}
bool pilot_record::combat_invincible() const {
  /// Combat and training retain separate invulnerability bits in the original setup byte
  return (data[0x23b] & 1u) != 0;
}
void pilot_record::combat_invincible(bool const value) {
  /// Preserve the independent training and flight-model choices
  data[0x23b] = static_cast<uint8_t>(value ? data[0x23b] | 1u : data[0x23b] & ~1u);
}
bool pilot_record::inflight_comments() const {
  /// The original setup byte stores suppression, so clear means comments enabled
  return (data[0x23b] & 0x40u) == 0;
}
void pilot_record::inflight_comments(bool const value) {
  /// Change only comment suppression, leaving the aircraft and training settings untouched
  data[0x23b] = static_cast<uint8_t>(value ? data[0x23b] & ~0x40u : data[0x23b] | 0x40u);
}
world::training_setup pilot_record::training_settings() const {
  /// MOAG stores site counts in KLA/AAA/vehicle order, followed by one-based qualities with zero meaning random
  return {
    .kla{
      .count{data[0x234]},
      .quality{data[0x237] - 1},
    },
    .vehicles{
      .count{data[0x236]},
      .quality{data[0x239] - 1},
    },
    .aaa{
      .count{data[0x235]},
      .quality{data[0x238] - 1},
    },
    .invincible{(data[0x23b] & 2u) != 0},
  };
}
void pilot_record::training_settings(world::training_setup const &settings) {
  /// Preserve the remaining setup bytes and aerodynamic-model bit when storing the original training controls
  for(auto const &defence : {settings.kla, settings.vehicles, settings.aaa}) {
    if(defence.count > world::training_defence::maximum_menu_count || defence.quality < -1 || defence.quality > 7) {
      throw std::invalid_argument{"training settings exceed the original menu limits"};
  }
    }
  data[0x234] = static_cast<uint8_t>(settings.kla.count);
  data[0x235] = static_cast<uint8_t>(settings.aaa.count);
  data[0x236] = static_cast<uint8_t>(settings.vehicles.count);
  data[0x237] = static_cast<uint8_t>(settings.kla.quality + 1);
  data[0x238] = static_cast<uint8_t>(settings.aaa.quality + 1);
  data[0x239] = static_cast<uint8_t>(settings.vehicles.quality + 1);
  if(settings.invincible != ((data[0x23b] & 2u) != 0)) {
    data[0x23b] = static_cast<uint8_t>((data[0x23b] & ~3u) | 4u | (settings.invincible ? 3u : 0u));
  }
}
bool pilot_record::beginner_flight() const {
  /// MOAG's aerodynamic-model toggle stores advanced mode in bit five of the pilot's setup byte
  return (data[0x23b] & 0x20u) == 0;
}
void pilot_record::beginner_flight(bool const value) {
  /// Preserve comments and the other original setup bits when changing only the flight model
  data[0x23b] = static_cast<uint8_t>(value ? data[0x23b] & ~0x20u : data[0x23b] | 0x20u);
}
pilot_condition pilot_record::condition(int32_t const now) const {
  /// Captivity and death take precedence over any unexpired court-martial sentence
  if((word(flags_offset) & 8u) != 0) return pilot_condition::captured;
  if((word(flags_offset) & 2u) != 0) return pilot_condition::killed;
  return suspended_until() > now ? pilot_condition::stockade : pilot_condition::ready;
}
bool pilot_record::unavailable() const {
  /// Original campaign processing distinguishes killed and captured pilots from active ones
  return (word(flags_offset) & 0xau) != 0;
}
void pilot_record::resurrect() {
  /// Resurrection preserves the career but clears the pilot's killed/captured status
  word(flags_offset, static_cast<uint16_t>(word(flags_offset) & ~0xau));
  word(resurrection_offset, static_cast<uint16_t>(word(resurrection_offset) + 1));
}
pilot_rank pilot_record::rank() const {
  /// Preserve all original rank labels even though normal retail promotion only reaches Major
  return static_cast<pilot_rank>(std::min<unsigned int>(data[rank_offset], std::to_underlying(pilot_rank::general)));
}
uint16_t pilot_record::sorties() const {
  /// The native mission counter belongs to the career, not an individual theatre
  return word(sorties_offset);
}
uint16_t pilot_record::resurrections() const {
  /// Career records and fastest-career tables retain the original revival counter
  return word(resurrection_offset);
}
uint8_t pilot_record::awards(medal const kind) const {
  /// Medal counts are individual bytes, not word counters or bits
  return data.at(std::to_underlying(kind));
}
int32_t pilot_record::suspended_until() const {
  /// MOAG stores a signed Unix-time release date after court-martial for friendly losses
  return dword(619);
}
int32_t pilot_record::career_score() const {
  /// Report the cumulative native signed career score
  return dword(career_score_offset);
}
int32_t pilot_record::last_sortie_score() const {
  /// MOAG's saved score can differ from ISCORE's report for successful suicide objectives or ejection/landing combinations
  return dword(latest_score_offset);
}
int32_t pilot_record::best_sortie_score() const {
  /// The highest-scoring complete sortie has its own statistics and original signed score
  return dword(best_statistics_offset + sortie_statistics::native_bytes);
}
sortie_statistics pilot_record::best_sortie() const {
  /// Best-sortie records describe one flight, rather than a mixture of individual peaks
  return sortie_statistics::decode(std::span{data}.subspan(best_statistics_offset, sortie_statistics::native_bytes));
}
sortie_statistics pilot_record::individual_records() const {
  /// Each native peak field is independently retained, with signed-word comparison and a dword time field
  return sortie_statistics::decode(std::span{data}.subspan(peak_statistics_offset, sortie_statistics::native_bytes));
}
sortie_statistics pilot_record::last_sortie() const {
  /// The latest result block starts immediately after the name, flags and career counters
  return sortie_statistics::decode(std::span{data}.subspan(latest_statistics_offset, sortie_statistics::native_bytes));
}
sortie_statistics pilot_record::career() const {
  /// Keep the verified cumulative statistics block distinct from the latest result
  return sortie_statistics::decode(std::span{data}.subspan(career_statistics_offset, sortie_statistics::native_bytes));
}
std::span<uint8_t const> pilot_record::native_data() const noexcept {
  /// Save containers can preserve the complete original record without mutable access to its invariants
  return data;
}
void pilot_record::award(medal const kind) {
  /// Original medal counts wrap as bytes
  ++data.at(std::to_underlying(kind));
}
void pilot_record::finish_sortie(sortie_statistics const &result, score_context const context) {
  /// Accumulate one counted sortie; the subsequent theatre scan owns promotions and awards
  if(context == score_context::campaign && !result.counts_for_career()) return;
  auto const raw{result.encode()};
  std::ranges::copy(raw, data.begin() + latest_statistics_offset);
  int32_t const score{calculate_score(result, context)};
  dword(latest_score_offset, score);
  dword(career_score_offset, maths::dword(static_cast<int64_t>(career_score()) + score));
  word(sorties_offset, static_cast<uint16_t>(sorties() + 1));
  if(score > dword(best_statistics_offset + sortie_statistics::native_bytes)) {
    std::copy_n(data.begin() + latest_statistics_offset, statistics_and_score_bytes, data.begin() + best_statistics_offset);
  }
  auto accumulate_word{[this](unsigned int const offset){
    uint16_t const recent{word(latest_statistics_offset + offset)};
    // MOAG compares individual records as signed words, then adds their raw word bits into the career totals
    if(maths::word(recent) > maths::word(word(peak_statistics_offset + offset))) word(peak_statistics_offset + offset, recent);
    word(career_statistics_offset + offset, static_cast<uint16_t>(word(career_statistics_offset + offset) + recent));
  }};
  for(unsigned int const offset : {4u, 6u, 72u, 74u, 76u}) accumulate_word(offset);
  std::array<unsigned int, 5> constexpr kill_count_offsets{8, 24, 40, 56, 114};
  for(unsigned int const first : kill_count_offsets) {
    for(unsigned int quality{0}; quality != 8; ++quality) accumulate_word(first + quality * 2);
  }
  unsigned int constexpr retained_time_offset{110};
  int32_t const recent_time{dword(latest_statistics_offset + retained_time_offset)};
  if(recent_time > dword(peak_statistics_offset + retained_time_offset)) dword(peak_statistics_offset + retained_time_offset, recent_time);
  dword(career_statistics_offset + retained_time_offset, maths::dword(static_cast<int64_t>(dword(career_statistics_offset + retained_time_offset)) + recent_time));
  if(result.has(sortie_status::killed)) word(flags_offset, static_cast<uint16_t>(word(flags_offset) | 2u));
  if(result.has(sortie_status::captured)) word(flags_offset, static_cast<uint16_t>(word(flags_offset) | 8u));
}
void pilot_record::review_career(std::span<theatre_progress> const theatres, uint16_t const theatre_aircraft_lost, int32_t const now, world::campaign_id const campaign) {
  /// MOAG scans the pilot's saved theatres after recording the sortie, except when friendly losses suspend a living pilot
  auto const result{last_sortie()};
  if(!unavailable() && result.friendly_destroyed != 0) {
    int32_t const sentence{maths::dword(static_cast<int64_t>(maths::word(result.friendly_destroyed)) * 86'400)};
    dword(619, maths::dword(static_cast<int64_t>(std::max(suspended_until(), now)) + sentence));
    return;
  }
  bool all_completed{true}, newly_completed{false};
  auto const definitions{world::campaign_theatres(campaign)};
  unsigned int required{0}, accomplished{0};
  for(auto const &definition : definitions) {
    bool completed{false};
    int16_t highest_completed{-1};
    if(definition.required) ++required;
    for(auto &theatre : theatres) {
      if(theatre.definition != definition.name) continue;
      highest_completed = std::max(highest_completed, theatre.objectives_completed);
      if(theatre.objectives_remaining == 0 && theatre.objectives_completed > 0) {
        completed = true;
        if(theatre.current && !theatre.completion_recorded) newly_completed = theatre.completion_recorded = true;
      }
    }
    if(highest_completed >= 20) ++accomplished;
    if(definition.required && !completed) all_completed = false;
  }
  auto const totals{career()};
  auto new_rank{rank()};
  if(maths::word(totals.objectives_completed) >= 1) new_rank = std::max(new_rank, pilot_rank::first_lieutenant);
  if(maths::word(totals.objectives_completed) >= 7) new_rank = std::max(new_rank, pilot_rank::captain);
  if(campaign == world::campaign_id::earth) {
    if(result.has(sortie_status::earth_saved)) new_rank = std::max(new_rank, pilot_rank::major);
  } else {
    if(maths::word(totals.objectives_completed) >= 20) new_rank = std::max(new_rank, pilot_rank::major);
    if(accomplished >= 2) new_rank = std::max(new_rank, pilot_rank::lieutenant_colonel);
    if(accomplished >= 3) new_rank = std::max(new_rank, pilot_rank::colonel);
    all_completed = all_completed && required >= 3;
    if(all_completed) new_rank = pilot_rank::general;
  }
  data[rank_offset] = std::to_underlying(new_rank);
  if(newly_completed) {
    award(medal::theatre_combat);
    if(theatre_aircraft_lost == 0) award(medal::distinguished_flying_cross);
  }
  if(all_completed && (word(flags_offset) & 2u) == 0) {
    if(campaign == world::campaign_id::other_worlds) data[std::to_underlying(medal::combat_master)] = 1;
    else if(awards(medal::combat_master) == 0) award(medal::combat_master);
  }
  int32_t const score{last_sortie_score()};
  if(maths::word(result.maximum_damage) >= 27 && result.has(sortie_status::home) && result.aircraft_lost == 0 && !result.has(sortie_status::crashed_away)) award(medal::purple_heart);
  if(score > 100'000) award(medal::stellar_cluster);
  else if(score > 50'000) award(medal::gold_star);
  else if(score > 20'000) award(medal::silver_star);
  else if(score > 10'000) award(medal::bronze_star);
  if(maths::word(result.objectives_completed) >= 5) award(medal::distinguished_service_cross);
  if(maths::word(sorties()) >= (awards(medal::service_award) + 1) * 10 && !result.has(sortie_status::killed)) award(medal::service_award);
}
pilot_roster::pilot_roster() {
  /// New rosters enable menu/title music, as the original launcher does
  header[music_flag_offset] = 255;
}
bool pilot_roster::music_enabled() const {
  /// MOAG's music toggle occupies byte twenty-one of the decoded global header
  // The earlier 23-byte roster predates this setting; byte twenty-one is its pilot count.
  return header.size() == legacy_header_bytes || header[music_flag_offset] != 0;
}
void pilot_roster::music_enabled(bool const value) {
  /// The DOS toggle stores true as FF and changes no other configuration bytes
  if(header.size() == legacy_header_bytes) header.insert(header.begin() + music_flag_offset, 0);
  header[music_flag_offset] = value ? 255 : 0;
}
bool pilot_roster::keyboard_control() const {
  /// The global roster header's third byte selects keyboard rather than joystick control in both retained layouts
  return header[2] != 0;
}
void pilot_roster::keyboard_control(bool const value) {
  /// Do not replace the remaining opaque native configuration header
  header[2] = static_cast<uint8_t>(value);
}
void pilot_roster::mask(std::span<uint8_t> const bytes, uint16_t const seed) noexcept {
  /// Pilot files use the Borland library rand stream, distinct from the flight engine's multiplicative PRNG
  uint32_t state{seed};
  for(auto &byte : bytes) {
    state = state * 0x015a4e35u + 1;
    byte ^= static_cast<uint8_t>(((state >> 16) & 32'767u) / 128u);
  }
}
pilot_roster pilot_roster::decode(std::span<uint8_t const> const source) {
  /// Validate both known original header sizes and preserve the interleaved extra byte verbatim
  resources::binary_reader const input{source};
  if(input.word(0) != 24 || source.size() < 28) throw std::invalid_argument{"unsupported native pilot file"};
  pilot_roster result;
  result.seed = input.word(2);
  std::vector<uint8_t> plain{source.begin() + 4, source.end()};
  mask(plain, result.seed);
  resources::binary_reader const decoded{plain};
  unsigned int header_length{0}, count{0};
  for(unsigned int const length : {24u, 23u}) {
    if(plain.size() >= length + 1) {
      unsigned int const records{decoded.word(length - 2)};
      if(plain.size() == length + 1 + records * 623u) {
        header_length = length;
        count = records;
        break;
      }
    }
  }
  if(header_length == 0) throw std::invalid_argument{"native pilot file count does not match its complete length"};
  result.header.assign(plain.begin(), plain.begin() + header_length);
  result.retained_byte = plain[header_length + (count == 0 ? 0 : 623)];
  for(unsigned int i{0}; i != count; ++i) result.pilots.emplace_back(std::span{plain}.subspan(header_length + i * 623 + (i == 0 ? 0 : 1), 623));
  return result;
}
std::vector<uint8_t> pilot_roster::encode() const {
  /// Reproduce native masking and record boundaries without interpreting or discarding unknown header bytes
  if(pilots.size() > 65'535) throw std::invalid_argument{"native pilot file has too many records"};
  std::vector<uint8_t> result{24, 0, static_cast<uint8_t>(seed), static_cast<uint8_t>(seed >> 8)};
  auto plain{header};
  plain[header.size() - 2] = static_cast<uint8_t>(pilots.size());
  plain[header.size() - 1] = static_cast<uint8_t>(pilots.size() >> 8);
  for(unsigned int i{0}; i != pilots.size(); ++i) {
    auto const record{pilots[i].native_data()};
    plain.insert(plain.end(), record.begin(), record.end());
    if(i == 0) plain.push_back(retained_byte);
  }
  if(pilots.empty()) plain.push_back(retained_byte);
  mask(plain, seed);
  result.insert(result.end(), plain.begin(), plain.end());
  return result;
}
std::string_view rank_abbreviation(pilot_rank const rank) {
  std::array<std::string_view, 7> constexpr names{"2nd Lt.", "1st Lt.", "Capt.", "Major", "Lt. Col.", "Colonel", "General"};
  return names.at(std::to_underlying(rank));
}
std::string_view rank_name(pilot_rank const rank) noexcept {
  /// Use the original rank labels for roster and record displays
  static std::array<std::string_view, 7> constexpr names{"2nd Lieutenant", "1st Lieutenant", "Captain", "Major", "Lt. Colonel", "Colonel", "General"};
  auto const index{std::to_underlying(rank)};
  return names[index < names.size() ? index : 0];
}

} // namespace corncob::game

#include "campaign.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <stdexcept>
#include <utility>
#include "maths/integer.h"
#include "platform/storage.h"
#include "resources/binary_reader.h"
#include "world/theatre_catalog.h"

namespace corncob::game {

namespace {

unsigned int constexpr theatre_header_bytes{671}, embedded_pilot_offset{48};
unsigned int constexpr stored_aircraft_offset{38}, remaining_objectives_offset{40}, initial_theatre_state_offset{44}, completion_recorded_offset{46};
int32_t wall_clock() {
  /// Native MOAG sentences use signed Unix seconds, independent of the flight simulation clock
  return maths::dword(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}
void store_word(std::span<uint8_t> const bytes, unsigned int const offset, uint16_t const value) {
  /// Write the known theatre-header words without replacing the retained opaque fields
  bytes[offset] = static_cast<uint8_t>(value);
  bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}
std::string definition_name(std::string_view const theatre) {
  /// The namespace belongs to the native port; DOS headers retain their original short definition stems.
  return std::string{world::find_theatre(theatre).dos_name};
}
std::string_view theatre_title(std::string_view const theatre) {
  return world::find_theatre(theatre).title;
}
unsigned int initial_stored_aircraft(std::string_view const theatre) {
  return world::find_theatre(theatre).reserve_aircraft;
}

std::string stored_text(std::span<uint8_t const> const field) {
  /// Fixed native fields are bounded even when imported padding is not terminated
  auto const end{std::ranges::find(field, 0)};
  return {reinterpret_cast<char const*>(field.data()), static_cast<size_t>(end - field.begin())};
}
std::string stored_definition(std::span<uint8_t const> const bytes) {
  /// Accept the earlier reconstruction's uppercase CCT suffix as well as original DOS stems
  auto name{stored_text(bytes.subspan(28, 10))};
  std::ranges::transform(name, name.begin(), [](unsigned char const c){return static_cast<char>(std::tolower(c));});
  if(name.ends_with(".cct")) name.resize(name.size() - 4);
  return name;
}
std::vector<std::filesystem::path> legacy_theatres(std::filesystem::path const &directory) {
  /// Earlier native builds placed slot-named snapshots beside the roster; keep their saved progress readable
  std::vector<std::filesystem::path> result;
  if(!std::filesystem::exists(directory)) return result;
  for(auto const &entry : std::filesystem::directory_iterator{directory}) {
    if(!entry.is_regular_file() || !entry.path().filename().string().starts_with("pilot_")) continue;
    auto extension{entry.path().extension().string()};
    std::ranges::transform(extension, extension.begin(), [](unsigned char const c){return static_cast<char>(std::tolower(c));});
    if(extension == ".tht") result.push_back(entry.path());
  }
  std::ranges::sort(result);
  return result;
}
std::vector<std::filesystem::path> owned_theatres(std::filesystem::path const &directory, pilot_record const &pilot) {
  /// Original records link numbered files explicitly; legacy slot-named snapshots predate those references
  std::vector<std::filesystem::path> result;
  for(auto const number : pilot.theatres()) {
    auto const path{directory / "THT" / (std::to_string(number) + ".THT")};
    if(std::filesystem::exists(path)) result.push_back(path);
  }
  for(auto const &path : legacy_theatres(directory)) {
    auto const bytes{platform::read_binary(path)};
    if(bytes.size() >= theatre_header_bytes && stored_text(std::span{bytes}.subspan(embedded_pilot_offset, 22)) == pilot.name()) result.push_back(path);
  }
  return result;
}

unsigned int remaining_objectives(world::theatre const &theatre) {
  /// INT's survey treats preset-complete anchors separately from objectives still available to the pilot
  return static_cast<unsigned int>(std::ranges::count_if(theatre.objects, [](auto const &object){
    if(!object.has(world::object_flag::present) || object.has(world::object_flag::objective_complete)) return false;
    using enum world::object_type;
    return object.type == generator_orb || object.type == destroyed_generator_orb || object.type == objective_saucer
      || (object.type == ground_crew && object.has(world::object_flag::special))
      || (object.type == portal && (object.quality() == 4 || object.quality() == 7));
  }));
}

} // anonymous namespace

campaign_store::campaign_profile &campaign_store::state() noexcept {
  return profiles[std::to_underlying(edition)];
}
campaign_store::campaign_profile const &campaign_store::state() const noexcept {
  return profiles[std::to_underlying(edition)];
}
campaign_store::campaign_store(std::filesystem::path directory) {
  /// Earth retains its existing DOS-compatible directory; expansion records have their own native containers.
  profiles[0].directory = std::move(directory);
  profiles[1].directory = profiles[0].directory / "other_worlds";
  load_profile();
  edition = world::campaign_id::other_worlds;
  load_profile();
  edition = world::campaign_id::earth;
  synchronise_identities();
}
void campaign_store::load_profile() {
  /// Load only user save state; original scenery remains embedded in the executable
  auto const path{state().directory / "PILOT.SCR"};
  if(std::filesystem::exists(path)) state().roster = pilot_roster::decode(platform::read_binary(path));
  for(unsigned int i{0}; i != state().roster.pilots.size(); ++i) {
    if(state().roster.pilots[i].selected() && !state().roster.pilots[i].unavailable()) {
      state().selected = i;
      break;
    }
  }
  auto const high_scores{state().directory / "PILOT.HSC"};
  if(std::filesystem::exists(high_scores)) state().scores = score_tables::decode(platform::read_binary(high_scores));
}
void campaign_store::synchronise_identities() {
  /// A common roster of names links independent careers; no medals, deaths or progress leak across editions.
  for(auto &source : profiles) for(auto const &pilot : source.roster.pilots) {
    for(auto &target : profiles) {
      if(std::ranges::none_of(target.roster.pilots, [&](auto const &entry){return entry.name() == pilot.name();})) {
        target.roster.pilots.emplace_back(pilot.name());
      }
    }
  }
}
world::campaign_id campaign_store::current_campaign() const noexcept {
  return edition;
}
void campaign_store::select_campaign(world::campaign_id const campaign) {
  /// Campaign choice changes the active record, while retaining the identity selected at the shared front end.
  if(edition == campaign) return;
  auto const name{active_pilot() ? active_pilot()->name() : std::string{}};
  synchronise_identities();
  edition = campaign;
  state().selected.reset();
  for(unsigned int i{0}; i != state().roster.pilots.size(); ++i) {
    bool const selected{!name.empty() && state().roster.pilots[i].name() == name};
    state().roster.pilots[i].select(selected);
    if(selected) state().selected = i;
  }
}
std::filesystem::path campaign_store::theatre_path(std::string_view const theatre) const {
  /// Recover the original ownership arrangement and allocate a free numbered path only for a new theatre
  if(!state().selected) throw std::logic_error{"training pilot has no persistent theatre"};
  auto const owned{owned_theatres(state().directory, state().roster.pilots[*state().selected])};
  auto const active{state().roster.pilots[*state().selected].active_theatre()};
  if(active && *active < owned.size()) {
    auto const bytes{platform::read_binary(owned[*active])};
    if(stored_definition(bytes) == definition_name(theatre)) return owned[*active];
  }
  for(auto const &path : owned) {
    auto const bytes{platform::read_binary(path)};
    if(stored_definition(bytes) == definition_name(theatre)) return path;
  }
  for(unsigned int index{1}; index <= 255; ++index) {
    auto const path{state().directory / "THT" / (std::to_string(index) + ".THT")};
    if(!std::filesystem::exists(path)) return path;
  }
  throw std::runtime_error{"no available saved-theatre filename"};
}
void campaign_store::save_roster() const {
  /// Write the original masked roster representation after a deliberate roster or sortie change
  for(auto const &profile : profiles) platform::write_binary(profile.directory / "PILOT.SCR", profile.roster.encode());
}
std::span<pilot_record const> campaign_store::pilots() const noexcept {
  /// The interface can inspect the roster without altering opaque original record fields
  return state().roster.pilots;
}
pilot_record const *campaign_store::active_pilot() const noexcept {
  /// No named active pilot means the original disposable trainee role
  return state().selected ? &state().roster.pilots[*state().selected] : nullptr;
}
bool campaign_store::can_fly() const {
  /// A selected career cannot launch while killed, captured or serving its original court-martial sentence
  auto const *pilot{active_pilot()};
  return !pilot || (!pilot->unavailable() && pilot->suspended_until() <= wall_clock());
}
unsigned int campaign_store::create_pilot(std::string_view const name, bool const activate_created) {
  /// Pilot creation validates the bounded native name before changing saved state
  if(std::ranges::any_of(state().roster.pilots, [name](pilot_record const &pilot){return pilot.name() == name;})) {
    throw std::invalid_argument{"a pilot with that name already exists"};
  }
  state().roster.pilots.emplace_back(name);
  synchronise_identities();
  unsigned int const index{static_cast<unsigned int>(state().roster.pilots.size() - 1)};
  if(activate_created) activate(index);
  else save_roster();
  return index;
}
void campaign_store::activate(unsigned int const index) {
  /// Activating one living pilot clears the selection marker from all other records
  if(index >= state().roster.pilots.size()) throw std::invalid_argument{"pilot selection is outside the roster"};
  if(state().roster.pilots[index].unavailable()) throw std::invalid_argument{"that pilot is killed or captured; resurrect the pilot first"};
  if(state().roster.pilots[index].suspended_until() > wall_clock()) throw std::invalid_argument{"that pilot is suspended after friendly losses; select another pilot"};
  for(unsigned int i{0}; i != state().roster.pilots.size(); ++i) state().roster.pilots[i].select(i == index);
  state().selected = index;
  save_roster();
}
void campaign_store::deactivate() {
  /// Deactivating returns to the disposable trainee without deleting saved careers
  for(auto &pilot : state().roster.pilots) pilot.select(false);
  state().selected.reset();
  save_roster();
}
void campaign_store::rename_pilot(unsigned int const index, std::string_view const name) {
  if(index >= state().roster.pilots.size()) throw std::invalid_argument{"pilot selection is outside the roster"};
  auto const previous{state().roster.pilots[index].name()};
  pilot_record validated{state().roster.pilots[index]};
  validated.rename(name);
  for(auto const &profile : profiles) for(auto const &pilot : profile.roster.pilots) {
    if(pilot.name() != previous && pilot.name() == name) throw std::invalid_argument{"a pilot with that name already exists"};
  }
  for(auto &profile : profiles) for(auto &pilot : profile.roster.pilots) {
    if(pilot.name() != previous) continue;
    auto const paths{owned_theatres(profile.directory, pilot)};
    pilot.rename(name);
    for(auto const &path : paths) {
      auto bytes{platform::read_binary(path)};
      std::ranges::copy(pilot.native_data().first(22), bytes.begin() + embedded_pilot_offset);
      platform::write_binary(path, bytes);
    }
    profile.scores.rename(previous, name);
    platform::write_binary(profile.directory / "PILOT.HSC", profile.scores.encode());
  }
  save_roster();
}
void campaign_store::resurrect(unsigned int const index) {
  /// Preserve the original resurrection counter while making the selected career available again
  if(index >= state().roster.pilots.size()) throw std::invalid_argument{"pilot selection is outside the roster"};
  if(!state().roster.pilots[index].unavailable()) throw std::invalid_argument{"that pilot is alive and free; resurrection is not needed"};
  state().roster.pilots[index].resurrect();
  save_roster();
}
void campaign_store::delete_pilot(unsigned int const index) {
  /// Deleting the shared identity retires both careers and removes only their explicitly owned worlds.
  if(index >= state().roster.pilots.size()) throw std::invalid_argument{"pilot selection is outside the roster"};
  auto const name{state().roster.pilots[index].name()};
  for(auto &profile : profiles) {
    auto const found{std::ranges::find_if(profile.roster.pilots, [&](auto const &pilot){return pilot.name() == name;})};
    if(found == profile.roster.pilots.end()) continue;
    unsigned int const removed{static_cast<unsigned int>(found - profile.roster.pilots.begin())};
    for(auto const &path : owned_theatres(profile.directory, *found)) std::filesystem::remove(path);
    profile.scores.retire(name);
    platform::write_binary(profile.directory / "PILOT.HSC", profile.scores.encode());
    profile.roster.pilots.erase(found);
    if(profile.selected && *profile.selected == removed) profile.selected.reset();
    else if(profile.selected && *profile.selected > removed) --*profile.selected;
  }
  save_roster();
}
bool campaign_store::music_enabled() const noexcept {
  /// Keep original global music preferences independent of the active pilot
  return profiles[0].roster.music_enabled();
}
void campaign_store::music_enabled(bool const value) {
  /// Persist the same music flag used by the original launcher
  for(auto &profile : profiles) profile.roster.music_enabled(value);
  save_roster();
}
bool campaign_store::keyboard_control() const noexcept {
  /// The original global control choice is independent of the active career
  return profiles[0].roster.keyboard_control();
}
void campaign_store::keyboard_control(bool const value) {
  /// Persist the digital control choice in its native header byte
  for(auto &profile : profiles) profile.roster.keyboard_control(value);
  save_roster();
}
void campaign_store::beginner_flight(bool const value) {
  /// A named career retains its original aerodynamic-model bit; the disposable trainee has no saved pilot record
  if(!state().selected) return;
  state().roster.pilots[*state().selected].beginner_flight(value);
  save_roster();
}
world::training_setup campaign_store::training_settings() const {
  /// Named pilots retain separate training preferences in their original setup bytes
  return state().selected ? state().roster.pilots[*state().selected].training_settings() : world::training_setup{};
}
void campaign_store::training_settings(world::training_setup const &settings) {
  /// The disposable trainee keeps menu-local choices; a named pilot saves them with its career
  if(!state().selected) return;
  state().roster.pilots[*state().selected].training_settings(settings);
  save_roster();
}
std::vector<saved_theatre> campaign_store::theatres() const {
  /// Menus inspect the same numbered THT files that flight resumes; opening a menu never changes a world
  std::vector<saved_theatre> result;
  if(!state().selected) {
    for(auto const &entry : state().trainee_theatres) result.push_back({
      .number{0},
      .definition{entry.definition},
      .title{theatre_title(entry.definition)},
      .objectives_remaining{maths::word(remaining_objectives(entry.state))},
      .pilot{},
      .stored_aircraft{entry.reserve_aircraft},
    });
    return result;
  }
  for(auto const &path : owned_theatres(state().directory, state().roster.pilots[*state().selected])) {
    auto const bytes{platform::read_binary(path)};
    if(bytes.size() < theatre_header_bytes) throw std::invalid_argument{"saved theatre header is truncated: " + path.string()};
    auto definition{stored_definition(bytes)};
    if(edition == world::campaign_id::other_worlds) definition = "other_worlds/" + definition;
    else {
      if(definition == "wastelnd") definition = "wasteland";
      if(definition == "whtsands") definition = "white_sands";
    }
    unsigned int const reserve{resources::binary_reader{bytes}.word(stored_aircraft_offset)};
    result.push_back({
      .number{path.parent_path() == state().directory / "THT" ? static_cast<uint8_t>(std::stoul(path.stem().string())) : uint8_t{0}},
      .definition{std::move(definition)},
      .title{stored_text(std::span{bytes}.first(28))},
      .objectives_remaining{resources::binary_reader{bytes}.signed_word(remaining_objectives_offset)},
      .pilot{std::span{bytes}.subspan(embedded_pilot_offset, 623)},
      .stored_aircraft{reserve},
    });
  }
  return result;
}
std::optional<unsigned int> campaign_store::active_theatre() const {
  /// The active index is local to this pilot's list, not a globally numbered filename
  return state().selected ? state().roster.pilots[*state().selected].active_theatre() : state().trainee_active_theatre;
}
void campaign_store::open_theatre(std::string_view const theatre, world::theatre const &world) {
  /// Opening creates the native empty theatre record immediately, before the first sortie
  select_campaign(world::find_theatre(theatre).campaign);
  if(!state().selected) {
    if(state().trainee_theatres.size() >= 20) throw std::invalid_argument{"The trainee already has twenty open theaters"};
    state().trainee_theatres.push_back({
      .definition{theatre},
      .state{world},
      .reserve_aircraft{initial_stored_aircraft(theatre)},
    });
    state().trainee_active_theatre = static_cast<unsigned int>(state().trainee_theatres.size() - 1);
    return;
  }
  auto &pilot{state().roster.pilots[*state().selected]};
  if(pilot.theatres().size() >= 20) throw std::invalid_argument{"This pilot already has twenty open theaters"};
  unsigned int number{1};
  while(number <= 255 && std::filesystem::exists(state().directory / "THT" / (std::to_string(number) + ".THT"))) ++number;
  if(number > 255) throw std::runtime_error{"no available saved-theatre filename"};
  std::vector<uint8_t> bytes(theatre_header_bytes);
  auto const title{theatre_title(theatre)};
  auto const definition{definition_name(theatre)};
  std::ranges::copy(title.substr(0, 27), bytes.begin());
  std::ranges::copy(definition, bytes.begin() + 28);
  store_word(bytes, stored_aircraft_offset, static_cast<uint16_t>(initial_stored_aircraft(theatre)));
  store_word(bytes, remaining_objectives_offset, static_cast<uint16_t>(remaining_objectives(world)));
  // Fresh native 3.42 base-theater files initialize this retained state word to one.
  store_word(bytes, initial_theatre_state_offset, 1);
  pilot_record empty{std::span<uint8_t const>{bytes}.subspan(embedded_pilot_offset, 623)};
  empty.rename(pilot.name());
  std::ranges::copy(empty.native_data(), bytes.begin() + embedded_pilot_offset);
  auto const objects{world::encode_theatre(world)};
  bytes.insert(bytes.end(), objects.begin(), objects.end());
  platform::write_binary(state().directory / "THT" / (std::to_string(number) + ".THT"), bytes);
  pilot.add_theatre(static_cast<uint8_t>(number));
  save_roster();
}
void campaign_store::activate_theatre(unsigned int const index) {
  /// Reuse the original numbered reference rather than copying or resetting its scenery
  auto const worlds{theatres()};
  if(index >= worlds.size()) throw std::invalid_argument{"No saved theater selected"};
  if(!state().selected) { state().trainee_active_theatre = index; return; }
  if(worlds[index].number == 0) throw std::invalid_argument{"Resume this older saved theater once before activating it here"};
  state().roster.pilots[*state().selected].add_theatre(worlds[index].number);
  save_roster();
}
void campaign_store::delete_theatre(unsigned int const index) {
  /// Only the explicitly selected pilot's world is removed; career statistics and other worlds remain
  auto const worlds{theatres()};
  if(index >= worlds.size()) throw std::invalid_argument{"No saved theater selected"};
  if(!state().selected) {
    state().trainee_theatres.erase(state().trainee_theatres.begin() + index);
    if(state().trainee_active_theatre) {
      if(*state().trainee_active_theatre == index) state().trainee_active_theatre.reset();
      else if(*state().trainee_active_theatre > index) --*state().trainee_active_theatre;
    }
    return;
  }
  auto const path{owned_theatres(state().directory, state().roster.pilots[*state().selected])[index]};
  std::filesystem::remove(path);
  state().roster.pilots[*state().selected].remove_theatre(worlds[index].number);
  save_roster();
}
bool campaign_store::combat_invincible() const {
  /// An unnamed trainee keeps its transient choices in the front end
  return active_pilot() && active_pilot()->combat_invincible();
}
void campaign_store::combat_invincible(bool const value) {
  /// Persist the original combat invulnerability option independently of training
  if(state().selected) { state().roster.pilots[*state().selected].combat_invincible(value); save_roster(); }
}
bool campaign_store::inflight_comments() const {
  /// Comments default to enabled for the disposable trainee
  return !active_pilot() || active_pilot()->inflight_comments();
}
void campaign_store::inflight_comments(bool const value) {
  /// Store the menu choice in the pilot's original setup byte
  if(state().selected) { state().roster.pilots[*state().selected].inflight_comments(value); save_roster(); }
}
void campaign_store::clear_scores(unsigned int const list) {
  /// Clearing one original score table preserves the other two
  state().scores.clear(list);
  platform::write_binary(state().directory / "PILOT.HSC", state().scores.encode());
}
std::optional<world::theatre> campaign_store::load_world(std::string_view const theatre) const {
  /// THT keeps the original 671-byte header and compact live world rather than resetting destroyed scenery each sortie
  if(world::find_theatre(theatre).campaign != edition) throw std::invalid_argument{"select the world's campaign before loading its saved theatre"};
  if(!state().selected) {
    if(state().trainee_active_theatre && state().trainee_theatres[*state().trainee_active_theatre].definition == theatre) return world::theatre{state().trainee_theatres[*state().trainee_active_theatre].state};
    return std::nullopt;
  }
  auto const path{theatre_path(theatre)};
  if(!std::filesystem::exists(path)) return std::nullopt;
  auto const data{platform::read_binary(path)};
  if(data.size() < theatre_header_bytes) throw std::invalid_argument{"saved theatre header is truncated: " + path.string()};
  std::string const expected{definition_name(theatre)};
  if(stored_definition(data) != expected) throw std::invalid_argument{"saved theatre refers to a different definition: " + path.string()};
  auto result{world::decode_theatre(std::span{data}.subspan(theatre_header_bytes))};
  auto original{world::load_theatre(theatre)};
  result.expansion = original.expansion;
  result.sky = original.sky;
  result.environment = original.environment;
  result.briefing = std::move(original.briefing);
  result.missions = std::move(original.missions);
  return result;
}
void campaign_store::finish_sortie(std::string_view const theatre, world::theatre const &world, sortie_statistics const &result, bool const reserve_aircraft) {
  /// Accumulate distinct global and per-theatre records, then run MOAG's original completion and award scan
  if(!state().selected) {
    if(theatre != "training" && state().trainee_active_theatre && state().trainee_theatres[*state().trainee_active_theatre].definition == theatre) {
      auto &saved{state().trainee_theatres[*state().trainee_active_theatre]};
      saved.state = world;
      if(reserve_aircraft && result.counts_for_career() && saved.reserve_aircraft != 0) --saved.reserve_aircraft;
    }
    save_scores(theatre, result);
    return;
  }
  auto &pilot{state().roster.pilots[*state().selected]};
  pilot.finish_sortie(result);
  bool const training{theatre == "training"};
  std::filesystem::path path;
  std::vector<uint8_t> saved;
  uint16_t theatre_aircraft_lost{0};
  if(!training) {
    path = theatre_path(theatre);
    if(path.parent_path() == state().directory / "THT") pilot.add_theatre(static_cast<uint8_t>(std::stoul(path.stem().string())));
    if(std::filesystem::exists(path)) {
      saved = platform::read_binary(path);
      if(saved.size() < theatre_header_bytes) throw std::invalid_argument{"saved theatre header is truncated: " + path.string()};
      saved.resize(theatre_header_bytes);
    } else {
      // original theatre records start with a name and empty statistics, without the global pilot's setup defaults
      saved.resize(theatre_header_bytes);
      store_word(saved, stored_aircraft_offset, static_cast<uint16_t>(initial_stored_aircraft(theatre)));
      store_word(saved, initial_theatre_state_offset, 1);
      pilot_record blank{std::span<uint8_t const>{saved}.subspan(embedded_pilot_offset, 623)};
      blank.rename(pilot.name());
      std::ranges::copy(blank.native_data(), saved.begin() + embedded_pilot_offset);
    }
    if(reserve_aircraft && result.counts_for_career()) {
      resources::binary_reader const input{saved};
      unsigned int const remaining{input.word(stored_aircraft_offset)};
      if(remaining != 0) store_word(saved, stored_aircraft_offset, static_cast<uint16_t>(remaining - 1));
    }
    pilot_record theatre_pilot{std::span<uint8_t const>{saved}.subspan(embedded_pilot_offset, 623)};
    theatre_pilot.finish_sortie(result);
    theatre_aircraft_lost = theatre_pilot.career().aircraft_lost;
    std::ranges::copy(theatre_pilot.native_data(), saved.begin() + embedded_pilot_offset);
    std::fill_n(saved.begin(), 38, 0);
    auto const title{theatre_title(theatre)};
    std::ranges::copy(title.substr(0, 27), saved.begin());
    auto const definition{definition_name(theatre)};
    std::ranges::copy(definition, saved.begin() + 28);
    store_word(saved, remaining_objectives_offset, static_cast<uint16_t>(remaining_objectives(world)));
  }
  if(result.counts_for_career()) {
    std::vector<theatre_progress> progress;
    auto survey{[&](std::span<uint8_t const> const bytes, bool const current){
      resources::binary_reader const input{bytes};
      auto name{stored_definition(bytes)};
      if(edition == world::campaign_id::other_worlds) name = "other_worlds/" + name;
      else {
        if(name == "wastelnd") name = "wasteland";
        if(name == "whtsands") name = "white_sands";
      }
      pilot_record const theatre_pilot{bytes.subspan(embedded_pilot_offset, 623)};
      progress.push_back({
        .definition{std::move(name)},
        .objectives_remaining{input.signed_word(remaining_objectives_offset)},
        .objectives_completed{maths::word(theatre_pilot.career().objectives_completed)},
        .completion_recorded{input.word(completion_recorded_offset) != 0},
        .current{current},
      });
    }};
    for(auto const &other : owned_theatres(state().directory, pilot)) {
      if(other == path) continue;
      auto const bytes{platform::read_binary(other)};
      if(bytes.size() < theatre_header_bytes) throw std::invalid_argument{"saved theatre header is truncated: " + other.string()};
      survey(bytes, false);
    }
    if(!training) survey(saved, true);
    pilot.review_career(progress, theatre_aircraft_lost, wall_clock(), edition);
    if(!training) store_word(saved, completion_recorded_offset, progress.back().completion_recorded);
  }
  if(!training) {
    auto const objects{world::encode_theatre(world)};
    saved.insert(saved.end(), objects.begin(), objects.end());
    platform::write_binary(path, saved);
  }
  save_roster();
  save_scores(theatre, result);
}
void campaign_store::save_scores(std::string_view const theatre, sortie_statistics const &result) {
  /// Persist all three native score lists independently of the selected pilot or live world
  state().scores.record(theatre == "training" ? "Training Mission" : theatre_title(theatre), result, active_pilot());
  platform::write_binary(state().directory / "PILOT.HSC", state().scores.encode());
}
score_tables const &campaign_store::high_scores() const noexcept {
  /// Reports read immutable ranked tables from the same save store as the roster
  return state().scores;
}
std::filesystem::path const &campaign_store::save_directory() const noexcept {
  /// Report the canonical user save location for help and diagnostics
  return state().directory;
}

} // namespace corncob::game

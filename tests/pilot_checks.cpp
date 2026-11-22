#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include "platform/storage.h"
#include "game/pilots.h"
#include "tests/reference_reader.h"

int main(int const argc, char const *const argv[]) {
  /// Check original masking, opaque-byte preservation and both retained native roster layouts
  if(argc != 2) throw std::invalid_argument{"pilot_checks requires a fixture path"};
  corncob::tests::reference_reader input{argv[1], "CORNPLT1"};
  auto const cases{input.dword()};
  for(unsigned int i{0}; i != cases; ++i) {
    auto const seed{input.word()}, bytes{input.word()};
    std::vector<uint8_t> actual(bytes);
    corncob::game::pilot_roster::mask(actual, seed);
    for(auto const byte : actual) if(byte != input.byte()) throw std::runtime_error{"native pilot masking stream differs"};
  }
  for(unsigned int const header : {23u, 24u}) {
    for(unsigned int count{0}; count != 4; ++count) {
      std::vector<uint8_t> plain(header, 0x71);
      plain[header - 2] = static_cast<uint8_t>(count);
      plain[header - 1] = 0;
      for(unsigned int i{0}; i != count; ++i) {
        std::array<uint8_t, 623> record;
        for(unsigned int n{0}; n != record.size(); ++n) record[n] = static_cast<uint8_t>(n * 73 + i);
        std::fill_n(record.begin(), 22, 0);
        std::string const name{"Native test " + std::to_string(i)};
        std::ranges::copy(name, record.begin());
        plain.insert(plain.end(), record.begin(), record.end());
        if(i == 0) plain.push_back(0x93);
      }
      if(count == 0) plain.push_back(0x93);
      corncob::game::pilot_roster::mask(plain, 73);
      std::vector<uint8_t> encoded{24, 0, 73, 0};
      encoded.insert(encoded.end(), plain.begin(), plain.end());
      auto const roster{corncob::game::pilot_roster::decode(encoded)};
      if(roster.pilots.size() != count || roster.encode() != encoded) throw std::runtime_error{"native pilot roster did not round trip"};
      auto with_music{roster};
      with_music.music_enabled(false);
      auto const reopened{corncob::game::pilot_roster::decode(with_music.encode())};
      if(reopened.music_enabled() || reopened.pilots.size() != roster.pilots.size()) throw std::runtime_error{"music setting overwrote a legacy pilot count"};
      for(unsigned int pilot{0}; pilot != count; ++pilot) {
        if(!std::ranges::equal(reopened.pilots[pilot].native_data(), roster.pilots[pilot].native_data())) throw std::runtime_error{"music setting changed an imported pilot"};
      }
    }
  }
  auto const directory{std::filesystem::path{argv[1]}.parent_path()};
  auto const keyboard{corncob::game::pilot_roster::decode(corncob::platform::read_binary(directory / "pilot_keyboard.bin"))};
  auto const joystick{corncob::game::pilot_roster::decode(corncob::platform::read_binary(directory / "pilot_joystick.bin"))};
  auto const linked{corncob::game::pilot_roster::decode(corncob::platform::read_binary(directory / "pilot_theatre.bin"))};
  if(!keyboard.keyboard_control() || joystick.keyboard_control() || !keyboard.pilots[0].beginner_flight() || linked.pilots[0].beginner_flight()) {
    throw std::runtime_error{"original saved control or aerodynamic-model choice decoded incorrectly"};
  }
  auto music_off{corncob::game::pilot_roster::decode(corncob::platform::read_binary(directory / "pilot_music_off.bin"))};
  if(!keyboard.music_enabled() || music_off.music_enabled()) throw std::runtime_error{"original saved music choice decoded incorrectly"};
  auto const before_music{music_off.encode()};
  music_off.music_enabled(true);
  auto const after_music{music_off.encode()};
  for(unsigned int i{0}; i != before_music.size(); ++i) {
    if(after_music[i] != static_cast<uint8_t>(i == 25 ? before_music[i] ^ 255 : before_music[i])) throw std::runtime_error{"music toggle changed another native roster byte"};
  }
  corncob::game::pilot_record created{"NativeTest"};
  created.select(true);
  if(!std::ranges::equal(created.native_data(), keyboard.pilots[0].native_data())) throw std::runtime_error{"new pilot defaults differ from original MOAG creation"};
  auto changed{keyboard};
  changed.keyboard_control(false);
  auto const old{keyboard.pilots[0].native_data()};
  changed.pilots[0].beginner_flight(false);
  for(unsigned int i{0}; i != old.size(); ++i) {
    if(changed.pilots[0].native_data()[i] != static_cast<uint8_t>(i == 0x23b ? old[i] | 0x20 : old[i])) throw std::runtime_error{"setting flight assistance changed opaque pilot state"};
  }
  auto const training{keyboard.pilots[0].training_settings()};
  if(training.kla.count != 5 || training.aaa.count != 6 || training.vehicles.count != 2
    || training.kla.quality != -1 || training.aaa.quality != -1 || training.vehicles.quality != 3 || !training.invincible) {
    throw std::runtime_error{"original new-pilot training defaults decoded incorrectly"};
  }
  auto configured{keyboard.pilots[0]};
  configured.training_settings(training);
  if(!std::ranges::equal(configured.native_data(), keyboard.pilots[0].native_data())) throw std::runtime_error{"unchanged training settings altered native pilot bytes"};
  created.add_theatre(1);
  created.beginner_flight(false);
  auto const references{linked.pilots[0].theatres()};
  if(references.size() != 1 || references[0] != 1 || created.native_data()[0x23d] != linked.pilots[0].native_data()[0x23d]) throw std::runtime_error{"original numbered-theatre references differ"};
  {
    /// MOAG records separate latest, best, peak and cumulative blocks; validate every affected byte against its complete original helper
    auto const fixture{(directory / "career.bin").string()};
    corncob::tests::reference_reader careers{fixture.c_str(), "CORNCRR1"};
    unsigned int const cases{careers.dword()};
    for(unsigned int i{0}; i != cases; ++i) {
      std::array<uint8_t, 623> before, expected;
      std::array<uint8_t, corncob::game::sortie_statistics::native_bytes> sortie;
      for(auto &byte : before) byte = careers.byte();
      for(auto &byte : sortie) byte = careers.byte();
      for(auto &byte : expected) byte = careers.byte();
      corncob::game::pilot_record actual{before};
      actual.finish_sortie(corncob::game::sortie_statistics::decode(sortie));
      for(unsigned int field{24}; field != 564; ++field) {
        if(actual.native_data()[field] != expected[field]) throw std::runtime_error{"native career case " + std::to_string(i) + " byte " + std::to_string(field)};
      }
    }
  }
  corncob::game::pilot_record pilot{"Native test"};
  corncob::game::sortie_statistics result{
    .objectives_completed{7},
  };
  pilot.finish_sortie(result);
  pilot.review_career({}, 0, 0);
  if(pilot.rank() != corncob::game::pilot_rank::captain || pilot.career().objectives_completed != 7) {
    throw std::runtime_error{"normal captain promotion or cumulative objectives failed"};
  }
  if(pilot.awards(corncob::game::medal::distinguished_service_cross) != 1) throw std::runtime_error{"five-objective medal failed"};
  auto const eligible_record{std::vector<uint8_t>{pilot.native_data().begin(), pilot.native_data().end()}};
  for(auto const excluded : {corncob::game::sortie_status::invincible, corncob::game::sortie_status::aborted}) {
    auto practice{result};
    practice.mark(excluded);
    practice.mark(corncob::game::sortie_status::killed);
    pilot.finish_sortie(practice);
    if(!std::ranges::equal(pilot.native_data(), eligible_record)) throw std::runtime_error{"unrecorded sortie changed the career or killed its pilot"};
  }
  std::cout << "16384 native pilot-mask bytes, both roster layouts and basic career progression exercised" << std::endl;
  return EXIT_SUCCESS;
}

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include "maths/random.h"
#include "platform/storage.h"
#include "game/high_scores.h"
#include "tests/reference_reader.h"

namespace {

void require(bool const condition, char const *const message) {
  /// Check ranking and native file integrity independently of debug assertions
  if(!condition) throw std::runtime_error{message};
}

} // anonymous namespace

int main(int const argc, char const *const argv[]) {
  /// Preserve all 5040 original-format bytes, update repeated careers and enforce fastest-career eligibility
  namespace game = corncob::game;
  std::array<uint8_t, game::score_tables::native_bytes> original;
  corncob::maths::random_source random;
  for(auto &byte : original) byte = static_cast<uint8_t>(random.next());
  require(game::score_tables::decode(original).encode() == original, "high-score codec discarded record or padding bytes");
  game::score_tables tables;
  game::pilot_record alba{"Alba"};
  game::pilot_record birch{"Birch"};
  game::sortie_statistics result{
    .objectives_completed{1},
  };
  result.mark(game::sortie_status::home);
  result.mark(game::sortie_status::landed);
  for(unsigned int flight{0}; flight != 2; ++flight) {
    alba.finish_sortie(result);
    tables.record("morning", result, &alba);
  }
  require(tables.fastest_careers().front().points == 0, "fastest-career list accepted fewer than three missions");
  alba.finish_sortie(result);
  tables.record("dusk", result, &alba);
  require(game::score_text(tables.best_careers().front().name) == "Alba" && tables.best_careers().front().points == 5625, "career totals or rank changed");
  require(tables.fastest_careers().front().sorties == 3 && tables.fastest_careers().front().points == 5625, "third sortie did not qualify for fastest-career ranking");
  require(std::ranges::count_if(tables.best_careers(), [](auto const &entry){return game::score_text(entry.name) == "Alba";}) == 1,
    "repeated sorties duplicated the same career");
  result.objectives_completed = 2;
  birch.finish_sortie(result);
  tables.record("wasteland", result, &birch);
  require(game::score_text(tables.best_missions().front().name) == "Birch" && tables.best_missions().front().points == 3750, "best-mission ranking lost a larger score");
  require(game::score_text(tables.best_careers().front().name) == "Alba", "mission and career ranking were conflated");
  require(game::score_tables::decode(tables.encode()).encode() == tables.encode(), "ranked score tables did not survive saving");
  if(argc != 2) throw std::invalid_argument{"high-score fixture directory required"};
  std::filesystem::path const reference{argv[1]};
  auto retired{game::score_tables::decode(corncob::platform::read_binary(reference / "scores_before_retirement.bin"))};
  auto const old_name{game::score_text(retired.best_missions()[0].name)};
  retired.retire(old_name);
  require(std::ranges::equal(retired.encode(), corncob::platform::read_binary(reference / "scores_retired.bin")), "native retirement changed unexpected bytes or used the wrong flag");
  retired.rename(old_name, "New career");
  require(game::score_text(retired.best_careers()[0].name) == old_name, "rename changed an already retired career");
  corncob::tests::reference_reader input{(reference / "high_scores.bin").string().c_str(), "CORNHSC1"};
  unsigned int const cases{input.dword()};
  for(unsigned int i{0}; i != cases; ++i) {
    std::string label(input.byte(), ' ');
    for(auto &letter : label) letter = static_cast<char>(input.byte());
    std::array<uint8_t, 623> pilot_bytes;
    for(auto &byte : pilot_bytes) byte = input.byte();
    game::pilot_record const pilot{pilot_bytes};
    std::array<uint8_t, game::score_tables::native_bytes> before;
    std::array<uint8_t, game::score_tables::native_bytes> after;
    for(auto &byte : before) byte = input.byte();
    for(auto &byte : after) byte = input.byte();
    auto tables{game::score_tables::decode(before)};
    tables.record(label, pilot.last_sortie(), &pilot);
    auto const actual{tables.encode()};
    if(actual != after) {
      auto const mismatch{std::ranges::mismatch(actual, after)};
      throw std::runtime_error{"native high-score insertion " + std::to_string(i) + " byte " + std::to_string(mismatch.in1 - actual.begin())};
    }
  }
  std::cout << cases << " complete native high-score insertion/tie/padding cases and connected career checks pass" << std::endl;
  return EXIT_SUCCESS;
}

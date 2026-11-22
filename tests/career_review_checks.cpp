#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "game/pilots.h"
#include "tests/reference_reader.h"

int main(int const argc, char const *const argv[]) {
  /// Exercise the full original campaign scan, including duplicate definitions, medal restrictions and signed boundaries
  if(argc < 2 || argc > 3) throw std::invalid_argument{"career review fixture required"};
  corncob::tests::reference_reader input{argv[1], "CORNREV1"};
  unsigned int const cases{input.dword()};
  auto const edition{argc == 3 ? corncob::world::campaign_id::other_worlds : corncob::world::campaign_id::earth};
  auto const definitions{corncob::world::campaign_theatres(edition)};
  for(unsigned int i{0}; i != cases; ++i) {
    std::array<uint8_t, 623> before;
    std::array<uint8_t, 623> expected;
    for(auto &byte : before) byte = input.byte();
    auto const now{input.signed_dword()};
    auto const losses{input.word()};
    unsigned int const count{input.byte()};
    std::vector<corncob::game::theatre_progress> theatres;
    for(unsigned int n{0}; n != count; ++n) {
      auto const definition{input.byte()};
      auto const remaining{input.signed_word()};
      auto const completed{input.signed_word()};
      bool const credited{input.byte() != 0};
      theatres.push_back({
        .definition{std::string{definitions[definition].name}},
        .objectives_remaining{remaining},
        .objectives_completed{completed},
        .completion_recorded{credited},
        .current{n == before[573]},
      });
    }
    for(auto &byte : expected) byte = input.byte();
    corncob::game::pilot_record actual{before};
    actual.review_career(theatres, losses, now, edition);
    for(unsigned int field{0}; field != expected.size(); ++field) {
      if(actual.native_data()[field] != expected[field]) throw std::runtime_error{"native review case " + std::to_string(i) + " byte " + std::to_string(field)};
    }
    for(auto const &theatre : theatres) {
      if(theatre.completion_recorded != (input.word() != 0)) throw std::runtime_error{"native theatre completion record " + std::to_string(i)};
    }
  }
  std::cout << cases << " complete original promotion, medal and court-martial decisions match" << std::endl;
  return EXIT_SUCCESS;
}

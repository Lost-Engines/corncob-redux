#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "audio/effects.h"
#include "resources/assets.h"
#include "tests/reference_reader.h"

int main(int const argc, char const *const argv[]) {
  /// Verify native engine writes and real non-silent PCM without requiring an installed audio device
  if(argc < 2 || argc > 3) throw std::invalid_argument{"audio_checks requires its fixture and optional raw float capture"};
  corncob::resources::collection resources{corncob::resources::front_end_asset()};
  corncob::audio::effects sound{resources};
  corncob::tests::reference_reader input{argv[1], "CORNSND1"};
  auto const count{input.dword()};
  std::array<float, 256> drain;
  sound.render(drain);
  if(!std::ranges::all_of(drain, [](float const sample){return sample == 0.0f;})) throw std::runtime_error{"inactive effects leaked into menu audio"};
  for(unsigned int i{0}; i != count; ++i) {
    corncob::audio::frame_state frame{
      .tick{(i + 1) * 40},
      .oil_pressure{input.word()},
      .forward_velocity{input.signed_word()},
      .active{true},
      .engine{true},
    };
    sound.update(frame);
    auto const writes{sound.pending_registers()};
    for(unsigned int n{0}; n != 2; ++n) {
      auto const address{input.byte()};
      auto const expected{input.byte()};
      auto const found{std::find_if(writes.rbegin(), writes.rend(), [address](auto const &entry){return entry.address == address;})};
      if(found == writes.rend() || found->value != expected) throw std::runtime_error{"engine OPL register mismatch in case " + std::to_string(i)};
    }
    sound.render(drain);
  }
  sound.reset();
  sound.update({
    .tick{40},
    .oil_pressure{20'000},
    .forward_velocity{6000},
    .active{true},
    .engine{true},
  });
  std::vector<float> samples(48'000 * 2);
  sound.render(samples);
  double energy{0};
  for(float const sample : samples) {
    if(!std::isfinite(sample) || sample < -1.0f || sample > 1.0f) throw std::runtime_error{"invalid PCM output"};
    energy += sample * sample;
  }
  if(energy < 0.01) throw std::runtime_error{"original engine patch produced silence"};
  if(argc == 3) {
    std::ofstream output{argv[2], std::ios::binary};
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output.write(reinterpret_cast<char const*>(samples.data()), static_cast<std::streamsize>(samples.size() * sizeof(float)));
  }
  sound.update({
    .tick{80},
    .active{false},
  });
  sound.render(drain);
  if(!std::ranges::all_of(drain, [](float const sample){return sample == 0.0f;})) throw std::runtime_error{"muted effects retained audible chip output"};
  for(unsigned int i{0}; i != 100; ++i) sound.update({.tick{1000 + i}});
  if(!sound.pending_registers().empty()) throw std::runtime_error{"muted sound accumulated register writes without playback"};
  sound.update({
    .tick{2000},
    .dead{true},
  });
  sound.render(drain);
  sound.update({
    .tick{2000},
    .dead{false},
  });
  sound.render(drain);
  if(!std::ranges::all_of(drain, [](float const sample){return sample == 0.0f;})) throw std::runtime_error{"leaving a death outcome kept effects audible at the stopped simulation tick"};
  std::cout << count << " original engine OPL write pairs match; live synthesis RMS " << std::sqrt(energy / static_cast<double>(samples.size())) << std::endl;
  return EXIT_SUCCESS;
}

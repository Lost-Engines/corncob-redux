#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include "audio/music.h"
#include "resources/assets.h"

extern "C" {
#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include <extras/stb_vorbis.c>
}

int main() {
  /// Verify delivered recordings, arbitrary callback boundaries, complete score transitions and silence alongside live effects
  namespace audio = corncob::audio;
  namespace resources = corncob::resources;
  std::array const assets{resources::title_music_asset(), resources::riding_music_asset(), resources::dreams_music_asset(),
    resources::voyage_music_asset(), resources::heavens_music_asset(), resources::daybreak_music_asset()};
  std::array<unsigned int, 6> constexpr native_boundary_ms{84'834, 204'155, 178'795, 233'746, 229'798, 152'967};
  using decoder = std::unique_ptr<stb_vorbis, decltype(&stb_vorbis_close)>;
  std::vector<decoder> direct;
  for(unsigned int i{0}; i != assets.size(); ++i) {
    int error{0};
    direct.emplace_back(stb_vorbis_open_memory(assets[i].data(), static_cast<int>(assets[i].size()), &error, nullptr), stb_vorbis_close);
    if(!direct.back()) throw std::runtime_error{"embedded music is not valid Vorbis"};
    auto const info{stb_vorbis_get_info(direct.back().get())};
    if(info.sample_rate != 48'000 || info.channels != 1 || stb_vorbis_stream_length_in_samples(direct.back().get()) != native_boundary_ms[i] * 48) {
      throw std::runtime_error{"music format or complete native score boundary differs"};
    }
  }
  audio::music player;
  std::array<float, 1024> actual;
  std::array<float, 1024> expected;
  actual.fill(0.125f);
  player.mix(actual);
  if(!std::ranges::all_of(actual, [](float const sample){return sample == 0.125f;})) throw std::runtime_error{"silent music erased sound effects"};
  auto verify{[&](audio::music_cue const cue, std::initializer_list<unsigned int> const order){
    player.select(cue);
    for(auto const track : order) {
      if(!stb_vorbis_seek_start(direct[track].get())) throw std::runtime_error{"cannot rewind reference recording"};
    }
    auto selected{order.begin()};
    unsigned int remaining{0};
    for(auto const track : order) remaining += native_boundary_ms[track] * 48;
    // Include the first second after the final EOF to verify the next repeat/playlist item.
    remaining += 48'000;
    float peak{0};
    unsigned int callback{0};
    while(remaining != 0) {
      unsigned int const frames{std::min(remaining, 1u + callback++ % 512)};
      auto block{std::span{actual}.first(frames * 2)};
      std::ranges::fill(block, 0.0f);
      player.mix(block);
      unsigned int filled{0};
      while(filled != frames) {
        auto const count{stb_vorbis_get_samples_float_interleaved(direct[*selected].get(), 2, expected.data() + filled * 2, static_cast<int>((frames - filled) * 2))};
        if(count == 0) {
          if(++selected == order.end()) selected = order.begin();
          if(!stb_vorbis_seek_start(direct[*selected].get())) throw std::runtime_error{"cannot repeat reference music"};
        } else {
          filled += static_cast<unsigned int>(count);
        }
      }
      for(unsigned int sample{0}; sample != frames * 2; ++sample) {
        if(!std::isfinite(block[sample]) || block[sample] != std::clamp(expected[sample], -1.0f, 1.0f)) {
          throw std::runtime_error{"music callback lost samples or used the wrong track at a score boundary"};
        }
        peak = std::max(peak, std::abs(block[sample]));
      }
      remaining -= frames;
    }
    if(peak < 0.05f) throw std::runtime_error{"embedded music is silent"};
    player.select(audio::music_cue::silent);
    actual.fill(0.25f);
    player.mix(actual);
    if(!std::ranges::all_of(actual, [](float const sample){return sample == 0.25f;})) throw std::runtime_error{"music continued into a silent scene"};
  }};
  verify(audio::music_cue::title, {0});
  verify(audio::music_cue::menus, {1, 2});
  verify(audio::music_cue::other_title, {3});
  verify(audio::music_cue::other_menus, {4, 5});
  verify(audio::music_cue::title, {0});
  std::cout << "Three native-driver recordings, full title/menu repeats and 1..512-frame audio callbacks match without gaps or external files" << std::endl;
  return EXIT_SUCCESS;
}

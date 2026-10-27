#include "music.h"
#include "gain.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <stdexcept>
#include "resources/assets.h"

extern "C" {
#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include <extras/stb_vorbis.c>
}

namespace corncob::audio {

struct music::implementation {
  gain_control gain;
  struct close_decoder {
    void operator()(stb_vorbis *const decoder) const noexcept {
      /// Vorbis decoders retain only their own playback state; embedded compressed bytes have static lifetime
      stb_vorbis_close(decoder);
    }
  };
  using decoder = std::unique_ptr<stb_vorbis, close_decoder>;
  std::array<decoder, 6> tracks;
  std::atomic<music_cue> requested{music_cue::silent};
  music_cue playing{music_cue::silent};
  unsigned int track{0};

  implementation() {
    /// Open embedded recordings before the audio callback starts; playback performs no filesystem I/O
    std::array const files{resources::title_music_asset(), resources::riding_music_asset(), resources::dreams_music_asset(),
      resources::voyage_music_asset(), resources::heavens_music_asset(), resources::daybreak_music_asset()};
    for(unsigned int i{0}; i != tracks.size(); ++i) {
      int error{0};
      if(files[i].size() > static_cast<unsigned int>(std::numeric_limits<int>::max())) throw std::invalid_argument{"embedded music exceeds decoder limits"};
      tracks[i].reset(stb_vorbis_open_memory(files[i].data(), static_cast<int>(files[i].size()), &error, nullptr));
      if(!tracks[i]) throw std::invalid_argument{"cannot decode embedded music: " + std::to_string(error)};
      auto const info{stb_vorbis_get_info(tracks[i].get())};
      if(info.sample_rate != 48'000 || info.channels != 1 || stb_vorbis_stream_length_in_samples(tracks[i].get()) == 0)
        throw std::invalid_argument{"embedded music must be nonempty 48 kHz mono Vorbis"};
    }
  }
};

music::music() : state{std::make_unique<implementation>()} {
  /// Lossless original-driver masters are encoded offline; the game only streams their compact Vorbis copies
}
music::~music() = default;
void music::volume(float const percent) noexcept { state->gain.set(percent); }
void music::select(music_cue const cue) noexcept {
  /// Only the requested programme crosses threads; the callback exclusively owns decoder cursors
  state->requested.store(cue, std::memory_order_relaxed);
}
void music::mix(std::span<float> const samples) noexcept {
  /// Add mono AdLib music equally to both output channels, advancing the menu playlist at complete song boundaries
  float const target_gain{state->gain.begin()};
  auto const requested{state->requested.load(std::memory_order_relaxed)};
  if(requested != state->playing) {
    state->playing = requested;
    state->track = requested == music_cue::other_title ? 3u : requested == music_cue::other_menus ? 4u : requested == music_cue::menus ? 1u : 0u;
    if(requested != music_cue::silent && !stb_vorbis_seek_start(state->tracks[state->track].get())) state->playing = music_cue::silent;
  }
  if(state->playing == music_cue::silent) { state->gain.settle(target_gain); return; }
  std::array<float, 1024> decoded;
  size_t offset{0};
  unsigned int empty_tracks{0};
  while(offset + 1 < samples.size()) {
    unsigned int const capacity{static_cast<unsigned int>(std::min(decoded.size(), (samples.size() - offset) & ~size_t{1}))};
    int const frames{stb_vorbis_get_samples_float_interleaved(state->tracks[state->track].get(), 2, decoded.data(), static_cast<int>(capacity))};
    if(frames == 0) {
      // Each edition repeats its title or advances its own two-song original ROLFILES.TXT playlist.
      state->track = state->playing == music_cue::title ? 0u : state->playing == music_cue::other_title ? 3u
        : state->playing == music_cue::other_menus ? (state->track == 4 ? 5u : 4u) : state->track == 1 ? 2u : 1u;
      if(++empty_tracks == state->tracks.size() || !stb_vorbis_seek_start(state->tracks[state->track].get())) return;
      continue;
    }
    empty_tracks = 0;
    for(unsigned int sample{0}; sample != static_cast<unsigned int>(frames) * 2; sample += 2) {
      float const gain{state->gain.next(target_gain)};
      for(unsigned int channel{0}; channel < 2; ++channel) {
        samples[offset] = std::clamp(samples[offset] + decoded[sample+channel]*gain, -1.0f, 1.0f);
        ++offset;
      }
    }
  }
}

} // namespace corncob::audio

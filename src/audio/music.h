#pragma once

#include <memory>
#include <span>
#include "audio/music_cue.h"

namespace corncob::audio {

class music {
private:
  struct implementation;
  std::unique_ptr<implementation> state;

public:
  music();
  ~music();
  void volume(float percent) noexcept;
  music(music const&) = delete;
  music &operator=(music const&) = delete;
  void select(music_cue cue) noexcept;
  void mix(std::span<float> samples) noexcept;
};

} // namespace corncob::audio

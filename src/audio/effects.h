#pragma once

#include <memory>
#include <span>
#include "resources/collection.h"

namespace corncob::game { class session; }

namespace corncob::audio {

struct frame_state {
  uint32_t tick{0};
  uint32_t gunshots{0};
  uint32_t missiles{0};
  uint32_t explosions{0};
  uint16_t damage{0};
  uint16_t oil_pressure{0};
  int16_t forward_velocity{0};
  uint16_t engine_damage{0};
  uint16_t orb_distance{63};
  uint8_t orb_frequency{128};
  uint16_t kla_distance{64};
  uint8_t kla_quality{0};
  uint16_t saucer_distance{256};
  uint8_t saucer_frequency{255};
  bool active{false};
  bool engine{false};
  bool on_foot{false};
  bool falling{false};
  bool stalled{false};
  bool dead{false};
};

struct register_write {
  uint8_t address;
  uint8_t value;
  uint32_t delay_frames{0};
};

frame_state observe(game::session const &game);

class effects {
private:
  struct implementation;
  std::unique_ptr<implementation> state;

public:
  static unsigned int constexpr sample_rate{48'000};
  static unsigned int constexpr channels{2};

  explicit effects(resources::collection const &resources);
  ~effects();
  void volume(float percent) noexcept;
  effects(effects const&) = delete;
  effects &operator=(effects const&) = delete;
  void reset();
  void reset(resources::collection const &resources);
  void update(frame_state const &frame);
  void render(std::span<float> samples) noexcept;
  std::vector<register_write> pending_registers() const;
};

} // namespace corncob::audio

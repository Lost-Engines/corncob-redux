#include "effects.h"
#include "gain.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <opl3.h>
#include "maths/random.h"
#include "maths/fixed.h"
#include "game/session.h"

namespace corncob::audio {

frame_state observe(game::session const &game) {
  /// Convert current gameplay state into the original channel-control quantities without exposing live objects to the PCM callback
  auto const observer{game.camera()};
  auto const &cues{game.sounds()};
  frame_state audio_frame{
    .tick{game.timer()},
    .gunshots{cues.gunshots},
    .missiles{cues.missiles},
    .explosions{cues.explosions},
    .damage{game.aircraft_damage().total},
    .oil_pressure{game.craft().oil_pressure},
    .forward_velocity{game.craft().body_velocity.x},
    .engine_damage{game.aircraft_damage().engine},
    .active{true},
    .engine{game.aircraft_damage().maximum_throttle != 0 && (game.player() == corncob::game::player_mode::in_aircraft || game.remote_control())},
    .on_foot{game.player() == corncob::game::player_mode::on_foot && !game.remote_control()},
    .falling{!game.pilot().standing},
    .stalled{game.craft().stalled},
    .dead{game.player() == corncob::game::player_mode::dead},
  };
  auto const &proximity{game.proximity_sounds()};
  audio_frame.orb_distance = proximity.orb_distance;
  audio_frame.kla_distance = proximity.kla_distance;
  audio_frame.kla_quality = proximity.kla_quality;
  audio_frame.saucer_distance = proximity.saucer_distance;
  audio_frame.saucer_frequency = proximity.saucer_frequency;
  if(observer.orb_distortion != 0) audio_frame.orb_frequency = static_cast<uint8_t>(128 * corncob::maths::multiply(corncob::maths::sine(static_cast<corncob::maths::angle>(game.frame_number() << 12)), corncob::maths::word(observer.orb_distortion)) / observer.orb_distortion);
  return audio_frame;
}

struct effects::implementation {
  gain_control gain;
  opl3_chip chip{};
  std::array<std::vector<register_write>, 12> patches;
  std::vector<register_write> queued;
  std::vector<register_write> consumed;
  struct scheduled_write {
    uint64_t frame;
    register_write write;
  };
  std::vector<scheduled_write> scheduled;
  uint64_t rendered_frames{0};
  uint32_t generation{0}, rendering_generation{0};
  mutable std::mutex queue_lock;
  frame_state previous;
  maths::random_source random;
  uint32_t boom_tick{0}, percussion_stop{0}, next_wind{0};
  unsigned int channel_three_patch{3};
  int kla_frequency{0};
  bool wind{false}, wind_trigger{false};

  void load_patches(resources::collection const &resources) {
    for(auto &patch : patches) patch.clear();
    /// Original ADL text contains twelve terminated lists of hexadecimal register/value pairs
    std::istringstream input{std::string{resources.text("3.adl")}};
    std::string line;
    unsigned int patch{0};
    while(std::getline(input, line)) {
      if(!line.empty() && line.back() == '\r') line.pop_back();
      if(line.empty()) continue;
      if(line == "ff") { ++patch; continue; }
      auto const comma{line.find(',')};
      unsigned int address{0}, value{0};
      if(patch >= patches.size() || comma == std::string::npos
        || std::from_chars(line.data(), line.data() + comma, address, 16).ec != std::errc{}
        || std::from_chars(line.data() + comma + 1, line.data() + line.size(), value, 16).ec != std::errc{}
        || address > 255 || value > 255) throw std::invalid_argument{"invalid embedded ADL effect bank"};
      patches[patch].push_back({
        .address{static_cast<uint8_t>(address)},
        .value{static_cast<uint8_t>(value)},
      });
    }
    if(patch != patches.size()) throw std::invalid_argument{"incomplete embedded ADL effect bank"};
  }
  explicit implementation(resources::collection const &resources) {
    load_patches(resources);
    queued.reserve(1024);
    consumed.reserve(1024);
    scheduled.reserve(1024);
    OPL3_Reset(&chip, sample_rate);
  }
  void write(uint8_t const address, uint8_t const value, uint32_t const delay = 0) {
    /// Queue hardware writes on the simulation thread; only the PCM thread operates the synth
    queued.push_back({
      .address{address},
      .value{value},
      .delay_frames{delay},
    });
  }
  void patch(unsigned int const index) {
    /// Shared channels are reprogrammed using their original complete register definitions
    for(auto const &entry : patches[index]) write(entry.address, entry.value);
  }
  void on(unsigned int const voice, uint16_t const pitch) {
    /// Preserve VOICEON's unusual pitch packing rather than substituting standard MIDI notes
    write(static_cast<uint8_t>(0xa0u + voice), static_cast<uint8_t>(pitch));
    write(static_cast<uint8_t>(0xb0u + voice), static_cast<uint8_t>(((pitch >> 8) << 2) | 0x21u));
  }
  void off(unsigned int const voice, uint16_t const pitch = 0x100) {
    /// VOICEOFF clears key-on while keeping the native block/frequency packing
    write(static_cast<uint8_t>(0xb0u + voice), static_cast<uint8_t>((((pitch >> 8) << 2) | 1u) & ~0x20u));
  }
  void silence() {
    /// Menu and paused states release voices without retaining a stuck rhythm trigger
    for(unsigned int voice{0}; voice != 9; ++voice) off(voice);
    write(0xbd, 0x20);
  }
  void initialise() {
    /// ADLREGINIT and INITVOICES retain the original OPL2 mode and effect-channel assignments
    for(unsigned int reg{0x40}; reg < 0x53; ++reg) write(static_cast<uint8_t>(reg), 63);
    for(unsigned int reg{0x53}; reg < 0x60; ++reg) write(static_cast<uint8_t>(reg), 20);
    for(unsigned int voice{0}; voice != 8; ++voice) write(static_cast<uint8_t>(0xb0u + voice), 0x0d);
    write(1, 0);
    write(1, 0x20);
    write(8, 0);
    for(unsigned int reg{0x20}; reg < 0x40; ++reg) write(static_cast<uint8_t>(reg), 1);
    for(unsigned int reg{0x60}; reg < 0x80; ++reg) write(static_cast<uint8_t>(reg), 0xf3);
    for(unsigned int reg{0x80}; reg < 0xa0; ++reg) write(static_cast<uint8_t>(reg), 3);
    for(unsigned int voice{0}; voice != 8; ++voice) write(static_cast<uint8_t>(0xa0u + voice), 0);
    write(0xbd, 0x20);
    for(unsigned int voice{0}; voice != 8; ++voice) write(static_cast<uint8_t>(0xc0u + voice), 0);
    for(unsigned int reg{0xe0}; reg < 0xf6; ++reg) write(static_cast<uint8_t>(reg), 0);
    patch(5);
    patch(1);
    patch(2);
    write(0xb6, 1);
    write(0x50, 0);
    write(0x53, 0);
    patch(0);
    patch(3);
    patch(8);
    patch(11);
    silence();
  }
};

effects::effects(resources::collection const &resources) : state{std::make_unique<implementation>(resources)} {
  /// Embed only original register patches; sound is synthesized live rather than sampled from DOS recordings
  reset();
}
effects::~effects() = default;
void effects::volume(float const percent) noexcept { state->gain.set(percent); }
void effects::reset(resources::collection const &resources) {
  /// Patch definitions belong to the simulation thread; the PCM thread consumes only queued register writes.
  state->load_patches(resources);
  reset();
}
void effects::reset() {
  /// A new sortie starts fresh effect bookkeeping while releasing any old chip envelopes
  std::lock_guard lock{state->queue_lock};
  state->queued.clear();
  ++state->generation;
  state->previous = {};
  state->random = maths::random_source{};
  state->boom_tick = state->percussion_stop = state->next_wind = 0;
  state->channel_three_patch = 3;
  state->kla_frequency = 0;
  state->wind = state->wind_trigger = false;
  state->initialise();
}
void effects::update(frame_state const &frame) {
  /// Connect original engine, proximity and event channels to the simulation clock
  std::lock_guard lock{state->queue_lock};
  auto const previous{state->previous};
  if(frame.active == previous.active && frame.dead == previous.dead && frame.tick == previous.tick) return;
  auto &sound{*state};
  sound.previous = frame;
  if(!frame.active && !frame.dead) {
    if(previous.active || previous.dead) sound.silence();
    return;
  }
  if(frame.active && !previous.active) {
    sound.initialise();
    sound.channel_three_patch = 3;
  }
  if(frame.engine && frame.forward_velocity >= 0) {
    unsigned int const pressure{static_cast<uint16_t>(frame.oil_pressure + 1920u)};
    auto const pitch{static_cast<uint16_t>(std::clamp((pressure + static_cast<unsigned int>(frame.forward_velocity)) / 80u, 64u, 1024u))};
    sound.on(0, pitch);
    sound.write(0x40, static_cast<uint8_t>(std::max(0, 16 - frame.engine_damage * 2)));
  } else sound.off(0, 0x40);
  if(frame.on_foot != previous.on_foot || !previous.active) {
    sound.patch(frame.on_foot ? 7 : 5);
    sound.wind = frame.on_foot;
  }
  if(frame.on_foot && frame.falling && frame.tick >= sound.next_wind) {
    sound.wind_trigger = !sound.wind_trigger;
    sound.write(0xbd, sound.wind_trigger ? 0x28 : 0x20);
    sound.next_wind = frame.tick + sound.random.below(4096);
  }
  if(frame.stalled && frame.engine) sound.on(5, 0x180);
  else sound.off(5, 0x180);
  if(frame.gunshots != previous.gunshots) {
    sound.off(1);
    sound.on(1, 0x200);
  }
  if(frame.missiles != previous.missiles && !sound.wind) {
    sound.write(0xbd, 0x20);
    sound.write(0xbd, 0x28);
    sound.percussion_stop = frame.tick + 128;
  }
  if(frame.explosions != previous.explosions && frame.tick - sound.boom_tick >= 512) {
    sound.patch(2);
    sound.off(2);
    sound.on(2, 0);
    sound.boom_tick = frame.tick;
  }
  if(frame.damage != previous.damage && !frame.dead) {
    sound.patch(4);
    sound.off(2);
    sound.on(2, 0x10);
  }
  if(frame.dead && !previous.dead) {
    sound.patch(6);
    sound.write(0xbd, 0x20);
    sound.write(0xbd, 0x28);
    sound.on(4, 0x10);
    sound.on(3, 0x300);
    sound.off(2, 0x300);
    sound.patch(4);
    sound.on(2, 0x10);
    // The original death sequence waits eighteen BIOS ticks, stops the scrape, then waits 750 ms.
    uint32_t constexpr noise_frames{static_cast<uint32_t>(18ull * 65'536 * sample_rate / 1'193'182)};
    sound.write(0xbd, 0x20, noise_frames);
    for(unsigned int voice{0}; voice != 9; ++voice) sound.write(static_cast<uint8_t>(0xb0u + voice), 5, noise_frames + sample_rate * 3 / 4);
    sound.percussion_stop = 0;
  }
  if(sound.percussion_stop != 0 && frame.tick >= sound.percussion_stop) {
    sound.write(0xbd, 0x20);
    sound.percussion_stop = 0;
  }
  if(frame.orb_distance < 63) {
    if(sound.channel_three_patch != 10) {
      sound.off(3, 0xff);
      sound.patch(10);
      sound.on(3, frame.orb_frequency);
      sound.channel_three_patch = 10;
    }
    auto const level{static_cast<uint8_t>(std::max(0, static_cast<int>(frame.orb_distance) - 13))};
    sound.write(0x4b, level);
    sound.write(0x48, level);
    sound.write(0xa3, frame.orb_frequency);
  } else {
    if(sound.channel_three_patch == 10) {
      sound.off(3, 0x10);
      sound.patch(3);
      sound.channel_three_patch = 3;
    }
    if(frame.saucer_distance < 255) {
      if(sound.channel_three_patch != 9) {
        sound.off(3, frame.saucer_distance);
        sound.patch(9);
        sound.on(3, 0xff);
        sound.channel_three_patch = 9;
      }
      sound.write(0x4b, static_cast<uint8_t>(frame.saucer_distance >> 2));
      sound.write(0xa3, frame.saucer_frequency);
    } else if(sound.channel_three_patch == 9) {
      sound.off(3, 0x10);
      sound.patch(3);
      sound.channel_three_patch = 3;
    }
  }
  sound.kla_frequency += (frame.kla_quality * 16 + 40 - sound.kla_frequency) >> 2;
  sound.write(0xa4, static_cast<uint8_t>(sound.kla_frequency));
  if(frame.kla_distance < 64) {
    sound.write(0x4c, static_cast<uint8_t>(std::max(8u, static_cast<unsigned int>(frame.kla_distance))));
    if(previous.kla_distance >= 64 || !previous.active) sound.on(4, static_cast<uint16_t>(frame.kla_quality << 5));
  } else if(previous.kla_distance < 64) {
    sound.write(0x4c, 63);
    sound.off(4, 63);
  }
}
void effects::render(std::span<float> const samples) noexcept {
  /// Drain complete register batches before rendering; the callback never reads mutable gameplay objects
  bool audible{false};
  {
    std::lock_guard lock{state->queue_lock};
    audible = state->previous.active || state->previous.dead;
    state->consumed.clear();
    state->consumed.swap(state->queued);
    if(state->rendering_generation != state->generation) {
      state->scheduled.clear();
      state->rendering_generation = state->generation;
    }
  }
  for(auto const &entry : state->consumed) state->scheduled.push_back({
    .frame{state->rendered_frames + entry.delay_frames},
    .write{entry},
  });
  std::ranges::stable_sort(state->scheduled, {}, &implementation::scheduled_write::frame);
  unsigned int event{0};
  float const target_gain{state->gain.begin()};
  for(size_t sample{0}; sample + 1 < samples.size(); sample += 2) {
    while(event < state->scheduled.size() && state->scheduled[event].frame <= state->rendered_frames) {
      auto const &entry{state->scheduled[event++].write};
      OPL3_WriteRegBuffered(&state->chip, entry.address, entry.value);
    }
    ++state->rendered_frames;
    std::array<int16_t, 2> frame;
    OPL3_GenerateResampled(&state->chip, frame.data());
    // Keep the chip clock running while muted, without leaking held envelopes into menu music.
    float const gain{state->gain.next(target_gain)};
    samples[sample] = audible ? frame[0] / 32768.0f * gain : 0.0f;
    samples[sample + 1] = audible ? frame[1] / 32768.0f * gain : 0.0f;
  }
  state->scheduled.erase(state->scheduled.begin(), state->scheduled.begin() + event);
}
std::vector<register_write> effects::pending_registers() const {
  /// Permit headless comparison of original sound protocols without opening an audio device
  std::lock_guard lock{state->queue_lock};
  return state->queued;
}

} // namespace corncob::audio

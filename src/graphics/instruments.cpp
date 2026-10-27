#include "instruments.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <utility>
#include "graphics/radar.h"

namespace corncob::graphics {

namespace {

screen_point needle(screen_point const &centre, screen_point const &diameter, maths::angle const angle) noexcept {
  /// ANGTOCORD rounds the high product word; the stored gauge sizes are diameters rather than radii
  auto rounded{[](int const value){ return (value + 0x8000) >> 16; }};
  return {centre.x + rounded(maths::sine(angle) * diameter.x), centre.y + rounded(maths::negate(maths::cosine(angle)) * diameter.y)};
}

} // anonymous namespace

void draw_gunsight(indexed_framebuffer &target, random_source &random, clip_rectangle const &bounds, screen_point const &centre, double const scale) {
  /// DRAWGS uses four double-width grey strokes in both cockpit and on-foot views
  raster sight{target, bounds, random};
  int const x{centre.x}, y{centre.y};
  if(scale != 1.0) {
    // Scale the four authored solid strokes, including their thickness and central gap.
    for(auto const &stroke : std::array<clip_rectangle,4>{{{-12,0,-3,2},{5,0,14,2},{0,-8,2,-2},{0,4,2,10}}}) {
      int const left{x + static_cast<int>(std::lround(stroke.left*scale))};
      int const top{y + static_cast<int>(std::lround(stroke.top*scale))};
      int const right{std::max(left+1,x + static_cast<int>(std::lround(stroke.right*scale)))};
      int const bottom{std::max(top+1,y + static_cast<int>(std::lround(stroke.bottom*scale)))};
      for(int row{std::max(top,bounds.top)}; row < std::min(bottom,bounds.bottom); ++row)
        sight.line({left,row},{right-1,row},7);
    }
    return;
  }
  for(int offset{0}; offset != 2; ++offset) {
    sight.line({x - 12, y + offset}, {x - 4, y + offset}, 7);
    sight.line({x + 13, y + offset}, {x + 5, y + offset}, 7);
    sight.line({x + offset, y - 8}, {x + offset, y - 3}, 7);
    sight.line({x + offset, y + 9}, {x + offset, y + 4}, 7);
  }
}

void recolour_button(indexed_framebuffer &target, cockpit_button const button, colour_index const source, colour_index const destination) {
  /// DRAWBUTTON searches all four colour planes, with an exclusive right edge and inclusive final row
  static std::array<clip_rectangle, 14> constexpr regions{
    clip_rectangle{
      .left{205},
      .top{209},
      .right{248},
      .bottom{218},
    },
    clip_rectangle{
      .left{205},
      .top{219},
      .right{248},
      .bottom{228},
    },
    clip_rectangle{
      .left{205},
      .top{229},
      .right{248},
      .bottom{241},
    },
    clip_rectangle{
      .left{443},
      .top{209},
      .right{484},
      .bottom{219},
    },
    clip_rectangle{
      .left{443},
      .top{220},
      .right{484},
      .bottom{230},
    },
    clip_rectangle{
      .left{443},
      .top{231},
      .right{484},
      .bottom{241},
    },
    clip_rectangle{
      .left{492},
      .top{209},
      .right{517},
      .bottom{218},
    },
    clip_rectangle{
      .left{492},
      .top{219},
      .right{517},
      .bottom{228},
    },
    clip_rectangle{
      .left{492},
      .top{229},
      .right{517},
      .bottom{238},
    },
    clip_rectangle{
      .left{279},
      .top{216},
      .right{324},
      .bottom{229},
    },
    clip_rectangle{
      .left{52},
      .top{251},
      .right{61},
      .bottom{262},
    },
    clip_rectangle{
      .left{59},
      .top{256},
      .right{68},
      .bottom{267},
    },
    clip_rectangle{
      .left{267},
      .top{231},
      .right{336},
      .bottom{241},
    },
    clip_rectangle{
      .left{524},
      .top{208},
      .right{629},
      .bottom{238},
    },
  };
  auto const &region{regions.at(std::to_underlying(button))};
  for(int y{region.top}; y <= region.bottom; ++y) {
    for(int x{region.left}; x < region.right; ++x) {
      auto &pixel{target.pixels[static_cast<unsigned int>(y) * 640 + static_cast<unsigned int>(x)]};
      if(pixel == source) pixel = destination;
    }
  }
}

void instrument_panel::draw(indexed_framebuffer &target, random_source &random, instrument_telemetry const &telemetry, unsigned int const frame) {
  /// Recreate INST's gauges and control marks while preserving the lower three planes of the cockpit artwork
  clip_rectangle constexpr screen{
    .left{0},
    .top{0},
    .right{640},
    .bottom{350},
  };
  raster high{target, screen, random, pixel_operation::high_plane};
  raster solid{target, screen, random};
  auto const &aircraft{telemetry.aircraft};
  auto gauge{[&](screen_point const &centre, screen_point const &diameter, maths::angle const angle){
    high.line(screen_point{centre}, needle(centre, diameter, angle), 8);
  }};
  // the original spreads these four instruments over fourteen alternating video-page frames
  switch(frame % 14) {
  case 0: case 1: temperature = static_cast<maths::angle>(aircraft.engine_temperature / 3 - 5400); break;
  case 2: case 3: case 8: case 9:
    pitch = static_cast<maths::angle>(-aircraft.attitude.pitch);
    if(maths::word(aircraft.attitude.roll + 16'384) < 0) pitch = static_cast<maths::angle>(-pitch + 32'768);
    break;
  case 4: case 5: case 10: case 11: compass = static_cast<maths::angle>(-aircraft.attitude.heading); break;
  default:
    {
      unsigned int const speed{static_cast<uint16_t>(aircraft.body_velocity.x) * (aircraft.variant == flight::aircraft_variant::experimental ? 4u : 1u)};
      airspeed = static_cast<maths::angle>((speed % 19'648) * 65'536 / 19'648);
    }
    break;
  }
  int32_t const height{std::max(0, aircraft.position.z - 720)};
  uint64_t const altitude_phase{static_cast<uint64_t>(height) * 65'536 / 3050};
  gauge({43, 267}, {46, 32}, static_cast<maths::angle>(altitude_phase));
  gauge({116, 267}, {46, 32}, airspeed);
  int const climb{std::clamp(static_cast<int>(aircraft.velocity.z) + 804, 0, 1607)};
  gauge({43, 318}, {46, 32}, static_cast<maths::angle>(static_cast<unsigned int>(climb) * 65'536 / 2414 + 27'306));
  gauge({32, 232}, {40, 28}, temperature);
  gauge({127, 224}, {20, 14}, compass);
  gauge({116, 318}, {10, 7}, aircraft.attitude.roll);
  auto horizon{[&](screen_point const &centre, screen_point const &diameter, maths::angle const angle){
    auto const end{needle(centre, diameter, static_cast<maths::angle>(angle + 16'384))};
    screen_point const opposite{centre * 2 - end};
    high.line(screen_point{opposite}, screen_point{end}, 8);
  }};
  horizon({116, 318}, {46, 32}, aircraft.attitude.roll);
  horizon({174, 224}, {30, 21}, pitch);
  auto const pitch_end{needle({174, 224}, {30, 21}, static_cast<maths::angle>(pitch + 16'384))};
  gauge(pitch_end, {10, 7}, pitch);
  solid.line({79, 224}, needle({79, 224}, {20, 14}, static_cast<maths::angle>((aircraft.oil_pressure / 3) * 4 - (32'767 - 5400))), telemetry.rocket_colour);
  solid.line({43, 267}, needle({43, 267}, {28, 20}, static_cast<maths::angle>(altitude_phase / 10)), 9);
  solid.line({43, 267}, needle({43, 267}, {24, 16}, static_cast<maths::angle>(altitude_phase / 100)), 7);
  high.disc({static_cast<int>(aircraft.throttle) * 148 / 200 + 478, 246}, 5, 8);
  high.line({385 + (telemetry.controls.roll + 512) * 44 / 1024, 230}, {385 + (telemetry.controls.roll + 512) * 44 / 1024, 234}, 8);
  high.line({385 + (-telemetry.controls.rudder + 512) * 44 / 1024, 214}, {385 + (-telemetry.controls.rudder + 512) * 44 / 1024, 218}, 8);
  high.line({365, 212 + (-telemetry.controls.pitch + 512) * 25 / 1024}, {371, 212 + (-telemetry.controls.pitch + 512) * 25 / 1024}, 8);
  int const flow_pitch{std::clamp(-static_cast<int>(std::bit_cast<int16_t>(maths::arctangent(aircraft.body_velocity.x, aircraft.body_velocity.z))) + 5400, 0, 10'799)};
  int const flow_yaw{std::clamp(static_cast<int>(std::bit_cast<int16_t>(maths::arctangent(aircraft.body_velocity.x, aircraft.body_velocity.y))) + 5400, 0, 10'799)};
  // DRAWMDS shares the radar window; an airflow angle outside it must not draw over neighbouring instruments.
  raster{target, radar_bounds, random}.disc({559 + flow_yaw * 160 / 10'800 - 80, 298 + flow_pitch * 110 / 10'800 - 55}, 6,
             aircraft.variant == flight::aircraft_variant::reserve ? 9 : 12);
  if(telemetry.damage != nullptr) {
    std::array<uint8_t, 13> constexpr colours{10, 10, 9, 9, 9, 9, 14, 14, 14, 14, 12, 12, 8};
    std::array<int, 4> constexpr rows{213, 219, 226, 233};
    for(unsigned int channel{0}; channel != rows.size(); ++channel) {
      auto const hits{telemetry.damage->indicator_hits[channel]};
      for(unsigned int bar{0}; bar < std::min<unsigned int>(hits, 13); ++bar) {
        uint8_t colour{colours[bar]};
        if(bar == 12 && ((frame * 40 / (hits > 14 ? 128 : hits > 13 ? 256 : 512)) & 1u) != 0) colour = 0;
        int const x{536 + static_cast<int>(bar) * 6};
        solid.line({x, rows[channel]}, {x + 5, rows[channel]}, colour);
      }
    }
  }
  auto button{[&](cockpit_button const control, uint8_t const foreground, uint8_t const background){
    recolour_button(target, control, 7, foreground);
    recolour_button(target, control, 0, background);
  }};
  button(cockpit_button::autopilot, telemetry.autopilot ? 15 : 7, telemetry.autopilot ? 1 : 0);
  button(cockpit_button::hold, telemetry.paused ? 15 : 7, telemetry.paused ? 5 : 0);
  button(cockpit_button::flaps, aircraft.flaps ? 15 : 7, aircraft.flaps ? 6 : 0);
  button(cockpit_button::stall, aircraft.stalled ? 14 : 7, aircraft.stalled ? 12 : 0);
  button(cockpit_button::complete, telemetry.completed ? 15 : 7, telemetry.completed ? 9 : 0);
  if(telemetry.damage && telemetry.damage->total >= 50) {
    bool const flash{(telemetry.timer & 512u) != 0};
    button(cockpit_button::eject, flash ? 15 : 7, flash ? 0 : 4);
  }
  bool const low_flash{telemetry.low_altitude && (telemetry.timer & 128u) != 0};
  button(cockpit_button::low_altitude, low_flash ? 14 : 7, telemetry.low_altitude && !low_flash ? 15 : 0);
  for(unsigned int bomb{0}; bomb != std::min(telemetry.bombs, 3u); ++bomb)
    recolour_button(target, static_cast<cockpit_button>(std::to_underlying(cockpit_button::first_bomb) + bomb), 7, 8);
  int32_t constexpr centimetres_per_foot{30}; // original coarse scale for the ten/twenty-thousand-foot annunciators
  if(aircraft.position.z >= 10'000 * centimetres_per_foot) recolour_button(target, cockpit_button::ten_thousand_feet, 7, 5);
  if(aircraft.position.z >= 20'000 * centimetres_per_foot) recolour_button(target, cockpit_button::twenty_thousand_feet, 7, 5);
  if(telemetry.damage && telemetry.damage->invincible) recolour_button(target, cockpit_button::damage, 0, 7);
}

} // namespace corncob::graphics

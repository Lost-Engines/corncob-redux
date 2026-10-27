#pragma once

#include <cstdint>
#include "flight/controls.h"
#include "flight/damage.h"
#include "graphics/raster.h"

namespace corncob::graphics {

enum class cockpit_button : uint8_t {
  autopilot, hold, flaps, stall, eject, complete,
  first_bomb, second_bomb, third_bomb, low_altitude,
  ten_thousand_feet, twenty_thousand_feet, help, damage,
};

void recolour_button(indexed_framebuffer &target, cockpit_button button, colour_index source, colour_index destination);

struct instrument_telemetry {
  flight::aircraft_state const &aircraft;
  flight::control_input controls;
  bool paused{false};
  bool completed{false};
  bool autopilot{false};
  bool low_altitude{false};
  uint16_t timer{0};
  unsigned int bombs{3};
  uint8_t rocket_colour{13};
  flight::damage_state const *damage{nullptr};
  bool slalom{false};
  unsigned int slalom_progress{0};
};

void draw_gunsight(indexed_framebuffer &target, random_source &random, clip_rectangle const &bounds, screen_point const &centre, double scale = 1.0);

class instrument_panel {
private:
  maths::angle airspeed{0};
  maths::angle temperature{0};
  maths::angle compass{0};
  maths::angle pitch{0};

public:
  void draw(indexed_framebuffer &target, random_source &random, instrument_telemetry const &telemetry, unsigned int frame);
};

} // namespace corncob::graphics

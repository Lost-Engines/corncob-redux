#pragma once

#include "vectorstorm/vector/vector2.h"
#include "resources/collection.h"

namespace corncob::graphics {

uint8_t title_letter_colour(uint8_t cloud, uint8_t stencil, uint8_t threshold) noexcept;

enum class title_stage { flyby, lettering };
enum class title_sprite_kind { saucer_1, saucer_2, saucer_3, saucer_4, flak_1, flak_2, flak_3, aircraft, flash };
struct title_sprite {
  title_sprite_kind kind;
  vec2<int> position{0, 0};
};
struct title_frame {
  title_stage stage{title_stage::flyby};
  int scroll{0};
  int sky_intensity{0};
  int mask_threshold{32};
  int credit_intensity{0};
  int credits_row{0};
  std::array<title_sprite, 5> sprites{};
  unsigned int sprite_count{0};
  bool stage_finished{false};
};
struct title_motion {
  title_stage stage{title_stage::flyby};
  uint16_t frame{0};
  int16_t fade{0};
  int16_t sky_intensity{0};
  int16_t terrain_scroll{0};
  int16_t cloud_scroll{0};
  int16_t aircraft_y{210};
  int16_t passes{0};
  bool first_pass{true};
  int16_t saucer_frame{4};
  vec2<int16_t> saucer{0, 0};
  int16_t explosion_timer{5};
  vec2<int16_t> explosion{0, 0};
  int16_t explosion_frame{0};
  int16_t mask_threshold{32};
  int16_t credits_row{0};
  uint32_t random_seed{1};

  void begin(title_stage next);
  title_frame advance();
};

class title_sequence {
private:
  title_motion motion;
  bool expansion;
  title_frame latest;
  double elapsed{0.0};
  unsigned int draw_page{0};
  bool change_stage{false};
  std::array<std::vector<uint8_t>, 2> pages;
  std::array<resources::indexed_image, 9> sprites;
  resources::indexed_image terrain;
  resources::indexed_image clouds;
  resources::indexed_image top_logo;
  resources::indexed_image bottom_logo;
  std::vector<uint8_t> credits;
  std::array<render::rgba_pixel, 256> palette;
  std::array<render::rgba_pixel, 256> flyby_palette;
  std::array<render::rgba_pixel, 256> lettering_palette;
  std::array<uint8_t, 768> sky_palette;

  void draw_frame(title_frame const &frame);

public:
  explicit title_sequence(resources::collection const &assets, bool expansion = false);
  void advance(double seconds);
  void render(render::framebuffer &target) const;
};

} // namespace corncob::graphics

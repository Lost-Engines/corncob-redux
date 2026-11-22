#include "title_sequence.h"
#include <algorithm>
#include "maths/fixed.h"
#include "graphics/interface.h"

namespace corncob::graphics {

namespace {

unsigned int constexpr width{320}, height{200}, credit_height{176};
int constexpr sky_fade_limit{111}, sky_fade_divisor{112}, fade_step{10};
int constexpr mask_maximum{224}, mask_step{32};
std::array<vec2<int>, 4> constexpr saucer_sizes{vec2<int>{44, 35}, vec2<int>{40, 35}, vec2<int>{42, 35}, vec2<int>{46, 35}};
std::array<vec2<int>, 3> constexpr explosion_sizes{vec2<int>{25, 21}, vec2<int>{33, 26}, vec2<int>{59, 37}};
uint8_t expand(uint8_t const component) {
  /// Expand the original six-bit VGA DAC without blending palette entries
  return static_cast<uint8_t>((component << 2) | (component >> 4));
}
int reflected_row(int row, int const source_height) {
  /// CCTITLE mirrors the loaded strip, then reverses that doubled strip again while scrolling
  int const doubled{source_height * 2};
  if(row >= doubled * 2) row -= doubled * 2;
  if(row >= doubled) row = doubled * 2 - row - 1;
  if(row >= source_height) row = doubled - row - 1;
  return row;
}

} // anonymous namespace

uint8_t title_letter_colour(uint8_t const cloud, uint8_t const stencil, uint8_t const threshold) noexcept {
  /// Mode X writes only the upper three bits from the thresholded letter mask; VGA latches retain the moving cloud detail
  return static_cast<uint8_t>((cloud & 31u) | (std::min(stencil, threshold) & 224u));
}
void title_motion::begin(title_stage const next) {
  /// The outer CCTITLE loop preserves random, saucer, explosion and credit continuation between its two stages
  stage = next;
  frame = 0;
  if(stage == title_stage::flyby) {
    fade = 0;
    sky_intensity = 0;
    terrain_scroll = 0;
    aircraft_y = 210;
    passes = 0;
  } else {
    fade = -1;
    mask_threshold = 32;
    cloud_scroll = 0;
  }
}
title_frame title_motion::advance() {
  /// Translate CCTITLE's two drawing loops; DOS music and port timing do not alter the authored motion arithmetic
  title_frame result{
    .stage{stage},
  };
  ++frame;
  auto random{[this]{random_seed = random_seed * 0x015a4e35u + 1; return static_cast<int>((random_seed >> 16) & 0x7fffu);}};
  auto sprite{[&](title_sprite_kind const kind, int const x, int const y){result.sprites[result.sprite_count++] = {
    .kind{kind},
    .position{x, y},
  };}};
  if(stage == title_stage::flyby) {
    if(--terrain_scroll < 0) terrain_scroll = 399;
    result.scroll = terrain_scroll;
    if(fade == 0 && static_cast<int16_t>(frame) > 5) {
      saucer_frame = static_cast<int16_t>((saucer_frame + 1) % 4);
      auto const &size{saucer_sizes[static_cast<unsigned int>(saucer_frame)]};
      saucer.y = maths::word((aircraft_y - 150) * 2 + size.y / 2);
      if(saucer.y < -200) saucer.x = maths::word((random() >> 8) + 160 - size.x / 2 - 64);
      if(saucer.x < 160) ++saucer.x;
      else if(saucer.x > 160) --saucer.x;
      sprite(static_cast<title_sprite_kind>(saucer_frame), saucer.x, saucer.y);
    }
    if((random() >> 9) + 10 < explosion_timer) explosion_timer = 0;
    if(fade == 0 && explosion_timer < 5) {
      std::array<int16_t, 5> constexpr order{0, 1, 1, 2, 2};
      explosion_frame = order[static_cast<unsigned int>(explosion_timer)];
      if(explosion_frame == 0) {
        explosion.x = maths::word((random() >> 7) + 32);
        explosion.y = maths::word((random() >> 8) + 36);
      } else explosion.y = maths::word(explosion.y + 11);
      auto const &size{explosion_sizes[static_cast<unsigned int>(explosion_frame)]};
      sprite(static_cast<title_sprite_kind>(static_cast<int>(title_sprite_kind::flak_1) + explosion_frame), explosion.x - size.x / 2, explosion.y - size.y / 2);
    }
    ++explosion_timer;
    if(fade == 0 && sky_intensity < sky_fade_limit) sky_intensity = static_cast<int16_t>(std::min(sky_fade_limit, sky_intensity + fade_step));
    if(fade == 255 && sky_intensity > 0) sky_intensity = static_cast<int16_t>(std::max(0, sky_intensity - fade_step));
    if(sky_intensity == sky_fade_limit) {
      sprite(title_sprite_kind::aircraft, 56, aircraft_y);
      aircraft_y = maths::word(aircraft_y - 4);
      if(aircraft_y < -121 - 4 * 90) {
        aircraft_y = 210 + 4 * 90;
        if(first_pass) { fade = 255; first_pass = false; } else if(passes < 2) ++passes;
        else fade = 255;
      }
      if(aircraft_y > -50 && aircraft_y < 160 && terrain_scroll % 2 == 0) {
        sprite(title_sprite_kind::flash, 56 + 52, aircraft_y + 22);
        sprite(title_sprite_kind::flash, 56 + 116, aircraft_y + 22);
      }
    } else aircraft_y = 210 + 4 * 30;
    result.sky_intensity = sky_intensity;
    result.stage_finished = fade == 255 && sky_intensity == 0;
  } else {
    if(--cloud_scroll < 0) cloud_scroll = 287;
    result.scroll = cloud_scroll;
    result.mask_threshold = fade == 0 ? mask_maximum : mask_threshold;
    if(fade == -1) {
      if(mask_threshold < mask_maximum) mask_threshold = static_cast<int16_t>(mask_threshold + mask_step);
      else fade = 0;
    }
    if(fade == 255) {
      if(mask_threshold > mask_step) mask_threshold = static_cast<int16_t>(mask_threshold - mask_step);
      else { result.stage_finished = true; return result; }
    }
    result.credit_intensity = mask_threshold * 42 / mask_maximum;
    result.credits_row = credits_row;
    if(++credits_row >= static_cast<int>(credit_height)) { credits_row = 0; fade = 255; }
  }
  return result;
}

title_sequence::title_sequence(resources::collection const &assets, bool const expansion) :
  expansion{expansion},
  sprites{
    assets.image("vga/skla1.vga"), assets.image("vga/skla2.vga"), assets.image("vga/skla3.vga"), assets.image("vga/skla4.vga"),
    assets.image("vga/ssflak1.vga"), assets.image("vga/ssflak2.vga"), assets.image("vga/ssflak3.vga"),
    assets.image("vga/hc6new.vga"), assets.image("vga/flash.vga"),
  },
  terrain{assets.image("vga/tnambc.vga")},
  clouds{assets.image("vga/tcloud5s.vga")},
  top_logo{assets.image(expansion ? "vga/cologo.vga" : "vga/tlogogr.vga")},
  bottom_logo{expansion ? resources::indexed_image{} : assets.image("vga/blogogr.vga")},
  credits(width * credit_height, 0),
  palette{sprites[0].palette} {
  /// Decode original artwork once; retain two indexed pages and the original masked cloud-fill effect
  for(auto &page : pages) page.resize(width * height, 0);
  std::ranges::copy(assets.file("vga/sky.pal"), sky_palette.begin());
  auto const base{assets.file("vga/hc6.pal")};
  for(unsigned int i{0}; i != 256; ++i) palette[i] = {
    .red{expand(base[i * 3])},
    .green{expand(base[i * 3 + 1])},
    .blue{expand(base[i * 3 + 2])},
  };
  std::array<uint8_t, 256> terrain_mapping;
  for(unsigned int colour_index{0}; colour_index != terrain_mapping.size(); ++colour_index) {
    auto const colour{terrain.palette[colour_index]};
    unsigned int closest{192}, distance{0xffffffffu};
    for(unsigned int i{192}; i != 224; ++i) {
      auto square{[](int const value){return value * value;}};
      unsigned int const difference{static_cast<unsigned int>(square((colour.red >> 2) - base[i * 3]) + square((colour.green >> 2) - base[i * 3 + 1]) + square((colour.blue >> 2) - base[i * 3 + 2]))};
      if(difference < distance) { closest = i; distance = difference; }
    }
    terrain_mapping[colour_index] = static_cast<uint8_t>(closest);
  }
  for(auto &pixel : terrain.pixels) pixel = terrain_mapping[pixel];
  flyby_palette = palette;
  auto const lettering{assets.file("vga/gradsky.pal")};
  for(unsigned int i{0}; i != 256; ++i) lettering_palette[i] = {
    .red{expand(lettering[i * 3])},
    .green{expand(lettering[i * 3 + 1])},
    .blue{expand(lettering[i * 3 + 2])},
  };
  std::string_view constexpr text{
    "\n     An MVP Software Production\n\n"
    " Design and Programming : Kevin Stokes\n"
    " Additional Programming : George Welch\n\n"
    " Music By :            James Collymore\n \n \n"
    " Play testing  :\n \n"
    " Lawrence Gilberti        Carl Schnurr\n"
    " Jeff Gardner          Darell Phillips\n"
    " Sean Gallaty              Thomas Pope\n\n"
  };
  int x{0}, y{0};
  auto const font{assets.file("font8.bin")};
  for(unsigned char const ch : text) {
    if(ch == '\n') { x = 0; y += 8; continue; }
    for(int row{0}; row != 8; ++row) for(int bit{0}; bit != 8; ++bit) {
      if(y + row < static_cast<int>(credit_height) && x + bit < static_cast<int>(width) && (font[ch * 8u + static_cast<unsigned int>(row)] & (128u >> bit))) {
        credits[static_cast<unsigned int>(y + row) * width + static_cast<unsigned int>(x + bit)] = 7;
    }
      }
    x += 8;
  }
}
void title_sequence::draw_frame(title_frame const &frame) {
  /// VGA plane masks replace the upper three index bits while retaining five cloud-detail bits under each letter
  draw_page ^= 1u;
  auto &pixels{pages[draw_page]};
  if(frame.stage == title_stage::flyby) {
    for(unsigned int y{0}; y != height; ++y) {
      int const source{reflected_row(frame.scroll + static_cast<int>(y), static_cast<int>(terrain.height))};
      std::copy_n(terrain.pixels.begin() + source * terrain.width, terrain.width, pixels.begin() + y * width);
    }
    for(unsigned int i{0}; i != frame.sprite_count; ++i) {
      auto const &command{frame.sprites[i]};
      auto const &image{sprites[static_cast<unsigned int>(command.kind)]};
      int const left{command.position.x / 4 * 4};
      for(unsigned int row{0}; row != image.height; ++row) for(unsigned int column{0}; column != image.width; ++column) {
        int const x{left + static_cast<int>(column)}, y{command.position.y + static_cast<int>(row)};
        uint8_t const colour{image.pixels[row * image.width + column]};
        if(colour != 0 && x >= 0 && x < static_cast<int>(width) && y >= 0 && y < static_cast<int>(height)) pixels[static_cast<unsigned int>(y) * width + static_cast<unsigned int>(x)] = colour;
      }
    }
    for(unsigned int i{0}; i != 32; ++i) palette[192 + i] = {
      .red{expand(static_cast<uint8_t>(sky_palette[(32 + i) * 3] * frame.sky_intensity / sky_fade_divisor))},
      .green{expand(static_cast<uint8_t>(sky_palette[(32 + i) * 3 + 1] * frame.sky_intensity / sky_fade_divisor))},
      .blue{expand(static_cast<uint8_t>(sky_palette[(32 + i) * 3 + 2] * frame.sky_intensity / sky_fade_divisor))},
    };
  } else {
    auto logo{[&](resources::indexed_image const &image, int const left, int const top){
      for(unsigned int row{0}; row != image.height; ++row) {
        int const source{reflected_row(frame.scroll + static_cast<int>(row) + top - 10, static_cast<int>(clouds.height))};
        for(unsigned int column{0}; column != image.width; ++column) {
          uint8_t const cloud{clouds.pixels[static_cast<unsigned int>(source) * clouds.width + column + static_cast<unsigned int>(left - 8)]};
          pixels[(row + static_cast<unsigned int>(top)) * width + column + static_cast<unsigned int>(left)] = title_letter_colour(cloud, image.pixels[row * image.width + column], static_cast<uint8_t>(frame.mask_threshold));
        }
      }
    }};
    if(expansion) logo(top_logo, 40, 10);
    else {
      logo(top_logo, 8, 10);
      logo(bottom_logo, 72, 64);
    }
    for(unsigned int row{0}; row != 55; ++row) {
      std::copy_n(credits.begin() + ((static_cast<unsigned int>(frame.credits_row) + row) % credit_height) * width, width, pixels.begin() + (row + 144) * width);
    }
    auto const intensity{expand(static_cast<uint8_t>(frame.credit_intensity))};
    palette[7] = {
      .red{intensity},
      .green{intensity},
      .blue{intensity},
    };
  }
}
void title_sequence::advance(double const seconds) {
  /// Pace the CPU-dependent DOS animation at twenty steps per second so flak and scrolling credits remain readable
  elapsed += seconds;
  double constexpr refresh{1.0 / 20.0};
  while(elapsed >= refresh) {
    elapsed -= refresh;
    if(change_stage) {
      motion.begin(motion.stage == title_stage::flyby ? title_stage::lettering : title_stage::flyby);
      palette = motion.stage == title_stage::flyby ? flyby_palette : lettering_palette;
      for(auto &page : pages) std::ranges::fill(page, 0);
      change_stage = false;
    }
    latest = motion.advance();
    if(latest.stage_finished) { change_stage = true; continue; }
    draw_frame(latest);
  }
}
void title_sequence::render(render::framebuffer &target) const {
  /// OpenGL receives only the finished CPU palette image, with no texture filtering or video playback
  target.width = width;
  target.height = height;
  target.pixels.resize(width * height);
  auto const &indices{pages[draw_page]};
  for(unsigned int i{0}; i != indices.size(); ++i) target.pixels[i] = palette[indices[i]];
}

} // namespace corncob::graphics

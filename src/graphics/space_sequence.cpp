#include "space_sequence.h"
#include <algorithm>
#include <cstdlib>

namespace corncob::graphics {

namespace {

void fill(indexed_framebuffer &target, unsigned int const byte_offset, unsigned int const bytes, unsigned int const rows, uint8_t const colour) {
  /// SRATS fills EGA byte strips; its cockpit borders deliberately overlap these view rectangles
  for(unsigned int row{0}; row != rows; ++row) {
    unsigned int const first{(byte_offset + row * 80) * 8};
    std::fill_n(target.pixels.begin() + first, bytes * 8, colour);
  }
}
void frames(indexed_framebuffer &target) {
  /// Retain EPAGE's byte-wide strips, including the asymmetric edges of the rear window
  fill(target, 320, 1, 195, 7);
  fill(target, 398, 1, 195, 7);
  fill(target, 0, 80, 8, 7);
  fill(target, 15'920, 80, 8, 7);
  fill(target, 19'940, 40, 5, 7);
  fill(target, 27'540, 40, 5, 7);
  fill(target, 20'340, 1, 91, 7);
  fill(target, 20'379, 1, 91, 7);
}

} // anonymous namespace

star_field::star_field(uint32_t const seed) : random{seed} {
  /// Begin the original library random stream once; warp restarts continue from its current state
  restart();
}
void star_field::restart() {
  /// The ending uses RAND_W multiplied by the requested range, rather than the flight engine's modulo helper
  auto scaled{[&](unsigned int const range){return static_cast<uint16_t>(static_cast<uint32_t>(random.next()) * range / 65'535u);}};
  for(auto &star : stars) {
    star = {
      .x{static_cast<int16_t>(scaled(8192) - 4096)},
      .y{static_cast<int16_t>(scaled(8192) - 4096)},
      .z{scaled(32'767)},
    };
  }
}
void star_field::advance(indexed_framebuffer &target) {
  /// Advance original EPSTR projection, preserving its near-plane guards, half-size coordinates and three intensities
  for(auto &star : stars) {
    star.z = static_cast<uint16_t>((star.z - 128u) & 32'767u);
    int const x{star.x * 3200};
    int const y{star.y * 3200};
    if(star.z <= 3200 && (std::abs(x / 640) >= star.z || std::abs(y / 457) >= star.z)) continue;
    int const projected_x{x / star.z + 640};
    int const projected_y{y / star.z + 200};
    if(projected_x < 0 || projected_x / 2 >= 639 || projected_y < 0 || projected_y >= 400) continue;
    uint8_t const colour{static_cast<uint8_t>(star.z > 26'000 ? 8 : star.z < 20'000 ? 15 : 7)};
    target.pixels[static_cast<unsigned int>(projected_y / 2) * 640 + static_cast<unsigned int>(projected_x / 2)] = colour;
  }
}
std::span<space_star const> star_field::positions() const noexcept {
  /// Native comparisons inspect coordinates without mutating the random stream
  return stars;
}
space_sequence::space_sequence(indexed_framebuffer const &backdrop, std::array<render::rgba_pixel, 16> const &colours) :
  pages{backdrop, backdrop},
  palette{colours} {
  /// SRATS inherits the flight page but runs in its own preserved 640x350 mode.
  if(backdrop.width != render::screen_layout::width || backdrop.height != render::screen_layout::height) {
    for(auto &page : pages) {
      page.width = render::screen_layout::width;
      page.height = render::screen_layout::height;
      page.monochrome = false;
      page.pixels.resize(page.width * page.height);
      for(unsigned int y{0}; y != page.height; ++y) {
        for(unsigned int x{0}; x != page.width; ++x) {
          page.pixels[y * page.width + x] = backdrop.pixels[(y * backdrop.height / page.height) * backdrop.width + x * backdrop.width / page.width];
        }
      }
    }
  }
  tick();
}
void space_sequence::tick() {
  /// The original runs once per BIOS tick, fades the sky for eight seconds and leaves Enter/Escape interactive
  if(complete) return;
  unsigned int constexpr fade_frames{8 * 18};
  frame = std::min(frame + 1, fade_frames);
  if(leaving && frame == fade_frames) {
    complete = true;
    return;
  }
  display_page ^= 1;
  auto &target{pages[display_page]};
  if(!warp) fill(target, 321, 78, 195, 11);
  fill(target, 20'341, 38, 90, leaving ? 11 : 0);
  if(frame >= fade_frames / 2) stars.advance(target);
  if(frame < fade_frames) {
    raster draw{target, leaving ? clip_rectangle{8, 4, 631, 199} : clip_rectangle{168, 254, 471, 344}, random};
    unsigned int const remaining{fade_frames - frame};
    draw.disc(leaving ? screen_point{319, 101} : screen_point{320, 299}, leaving ? 100 : static_cast<int>(remaining * remaining * 1024 / (fade_frames * fade_frames)), 2);
  }
  frames(target);
  if(warp && ++warp_frames > 270) {
    /// The retained 3.42 executable flashes white and returns from warp; the published source later made this registered-only behaviour optional
    warp = false;
    leaving = true;
    frame = 0;
    stars.restart();
    palette[11] = {255, 255, 255};
    for(auto &page : pages) {
      fill(page, 321, 78, 195, 11);
      fill(page, 20'341, 38, 90, 11);
      frames(page);
    }
  }
}
void space_sequence::advance(double const seconds) {
  /// Keep the original starfield clock independent of display refresh and the stopped flight simulation
  clock += seconds;
  while(clock >= tick_period && !complete) {
    tick();
    clock -= tick_period;
  }
}
void space_sequence::toggle_warp() noexcept {
  /// Enter preserves old star pixels independently on the two alternating EGA pages
  warp = !warp;
}
bool space_sequence::finished() const noexcept {
  /// Escape is handled by the front end; the native warp flash also completes after its second fade
  return complete;
}
void space_sequence::render(render::framebuffer &target) const {
  /// Only sky palette entry eleven fades; earth, cockpit and star intensities keep their original colours
  unsigned int constexpr fade_frames{8 * 18};
  unsigned int const factor{255u - std::min(frame, fade_frames) * 255u / fade_frames};
  auto colours{palette};
  for(auto *channel : {&colours[11].red, &colours[11].green, &colours[11].blue}) {
    unsigned int const value{((*channel >> 2) * factor) >> 8};
    *channel = static_cast<uint8_t>((value << 2) | (value >> 4));
  }
  target.width = 640;
  target.height = 350;
  target.pixels.resize(render::screen_layout::pixel_count);
  for(unsigned int i{0}; i != target.pixels.size(); ++i) target.pixels[i] = colours[pages[display_page].pixels[i] & 15u];
}

} // namespace corncob::graphics

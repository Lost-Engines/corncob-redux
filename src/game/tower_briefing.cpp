#include "tower_briefing.h"
#include <algorithm>
#include <format>
#include <sstream>
#include <stdexcept>
#include "graphics/interface.h"
#include "graphics/menu_canvas.h"
#include "render/palette.h"

namespace corncob::game {

namespace {

std::string_view read_line(std::string_view &text) {
  /// TOWER uses fgets(buffer, 128, file): a long physical line becomes separate rows of at most 127 bytes
  auto const newline{text.find('\n')};
  auto const length{std::min<size_t>(127, newline == std::string_view::npos ? text.size() : newline + 1)};
  auto const result{text.substr(0, length)};
  text.remove_prefix(length);
  return result;
}

} // anonymous namespace

tower_briefing::tower_briefing(std::string_view text, unsigned int const airfield) :
  airfield{airfield} {
  /// The helper's zero-based airfield argument selects the following form-feed section, skipping the theatre description
  if(airfield >= airfield_count) throw std::out_of_range{"tower airfield must be between zero and eight"};
  for(unsigned int section{0}; section <= airfield; ++section) {
    while(!text.empty()) {
      if(read_line(text).starts_with('\f')) break;
    }
  }
  while(!text.empty()) {
    auto line{read_line(text)};
    if(line.starts_with('\f')) break;
    /// Its second read strips up to two CR/LF terminators, retaining leading blanks and empty rows
    for(unsigned int n{0}; n != 2 && !line.empty() && (line.back() == '\r' || line.back() == '\n'); ++n) line.remove_suffix(1);
    text_lines.emplace_back(line.substr(0, line.find('\0')));
  }
}
void tower_briefing::move(int const lines) {
  /// Arrow navigation clamps at either end; U/D advance eleven rows and PgUp/PgDn advance twenty
  first = static_cast<unsigned int>(std::clamp<int64_t>(static_cast<int64_t>(first) + lines, 0, maximum_scroll()));
}
void tower_briefing::home() {
  first = 0;
}
void tower_briefing::end() {
  first = maximum_scroll();
}
unsigned int tower_briefing::first_line() const noexcept {
  return first;
}
unsigned int tower_briefing::maximum_scroll() const noexcept {
  return text_lines.size() > visible_lines ? static_cast<unsigned int>(text_lines.size() - visible_lines) : 0;
}
std::span<std::string const> tower_briefing::lines() const noexcept {
  return text_lines;
}
std::optional<world::mission_start> tower_briefing::selected_start() const {
  /// TOWER highlights and returns the first Sxyth directive within the visible window, rather than a separate list selection
  std::string title;
  for(unsigned int n{0}; n < std::min<unsigned int>(static_cast<unsigned int>(text_lines.size()), first + visible_lines); ++n) {
    auto const &line{text_lines[n]};
    if(line.find("Mission Code Name") != std::string::npos) {
      auto const colon{line.find(':')};
      if(colon != std::string::npos) {
        title = line.substr(colon + 1);
        title.erase(0, title.find_first_not_of(' '));
      }
    }
    if(n < first || !line.starts_with("Sxyth ")) continue;
    world::mission_start result{
      .title{title},
    };
    std::istringstream input{line.substr(6)};
    int32_t heading{0};
    if(!(input >> result.position.x >> result.position.y >> result.position.z >> heading)) return std::nullopt;
    result.heading = static_cast<maths::angle>(heading);
    return result;
  }
  return std::nullopt;
}
void tower_briefing::render(render::framebuffer &target, std::span<uint8_t const> const font) const {
  /// TOWER.EXE draws an unlabelled single frame, 22 text rows, a right-hand scroll thumb and an inverse footer
  graphics::interface_canvas canvas{target, font, 16, 640, 400};
  graphics::menu_canvas screen{canvas};
  screen.colours(7, 1, 7, 15);
  canvas.clear(render::ega_palette[1]);
  screen.panel(0, 0, 79, 24, 7, false, false);
  if(text_lines.empty()) {
    screen.text(1, 1, std::format("There is no information to print here, number={}", airfield));
    screen.text(2, 3, "Type almost any key to continue...");
    return;
  }
  bool selected{false};
  bool highlighted{false};
  unsigned int const count{static_cast<unsigned int>(text_lines.size())};
  for(unsigned int row{0}; row != visible_lines; ++row) {
    unsigned int const index{first + row};
    if(index < count) {
      auto const &line{text_lines[index]};
      if(!selected && line.starts_with("Sxyth ")) {
        screen.rich_text(1, static_cast<int>(row + 1), "\\(" + line + "\\)", 7, line_width, highlighted);
        selected = true;
      } else {
        screen.rich_text(1, static_cast<int>(row + 1), line, 7, line_width, highlighted);
      }
    }
    if(count > visible_lines) {
      unsigned int const top{first * visible_lines / count};
      unsigned int const bottom{top + visible_lines * visible_lines / count};
      char const thumb{static_cast<char>(row >= top && row <= bottom ? 219 : 177)};
      screen.text(78, static_cast<int>(row + 1), std::string_view{&thumb, 1}, highlighted ? 15 : 7);
    }
  }
  std::string_view const footer{count > visible_lines ? "Type \x18, \x19, PGUP, PGDN,   <CR> to select,   <esc> to cancel" : "Type <CR> to return to the game..."};
  screen.text(1, 23, std::string(line_width + 1, ' '), 15, 0);
  screen.text(1 + static_cast<int>((line_width + 1 - footer.size()) / 2), 23, footer, 15, 0);
}

} // namespace corncob::game

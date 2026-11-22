#include "collection.h"
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include "resources/binary_reader.h"

namespace corncob::resources {

namespace {

std::string line(std::istream &input) {
  /// Preserve CP437 text while removing only DOS line terminators
  std::string result;
  if(!std::getline(input, result)) throw std::invalid_argument{"truncated original presentation script"};
  if(!result.empty() && result.back() == '\r') result.pop_back();
  return result;
}
int number(std::istream &input) {
  /// Original layout scripts store one decimal field per line
  return std::stoi(line(input));
}
std::string filename(std::istream &input) {
  /// Embedded collection names use portable lowercase slashes rather than DOS path separators
  auto result{line(input)};
  for(auto &ch : result) {
    if(ch == '\\') ch = '/';
    if(ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + 'a' - 'A');
  }
  return result;
}

} // anonymous namespace

collection::collection(std::span<uint8_t const> const source, collection const *const fallback) : fallback{fallback} {
  /// Index immutable named assets without unpacking them onto the user's filesystem
  if(source.size() < 10 || std::string_view{reinterpret_cast<char const *>(source.data()), 8} != "CORNUI01") {
    throw std::invalid_argument{"invalid embedded presentation collection"};
  }
  binary_reader input{source};
  unsigned int offset{10};
  for(unsigned int n{0}; n != input.word(8); ++n) {
    unsigned int const name_length{input.byte(offset)};
    unsigned int const data_length{input.dword(offset + 1)};
    offset += 5;
    if(name_length > source.size() - std::min<size_t>(offset, source.size())) throw std::invalid_argument{"truncated embedded asset name"};
    std::string_view const name{reinterpret_cast<char const *>(source.data() + offset), name_length};
    offset += name_length;
    if(data_length > source.size() - std::min<size_t>(offset, source.size())) throw std::invalid_argument{"truncated embedded asset data"};
    if(!entries.emplace(name, source.subspan(offset, data_length)).second) throw std::invalid_argument{"duplicate embedded asset name"};
    offset += data_length;
  }
  if(offset != source.size()) throw std::invalid_argument{"unexpected embedded collection trailer"};
}
std::span<uint8_t const> collection::file(std::string_view const name) const {
  /// Keep missing-resource errors specific without accessing an external installation
  auto const found{entries.find(name)};
  if(found == entries.end() && fallback) return fallback->file(name);
  if(found == entries.end()) throw std::invalid_argument{"embedded asset missing: " + std::string{name}};
  return found->second;
}
std::string_view collection::text(std::string_view const name) const {
  /// Original scripts and help retain their DOS character bytes until the BIOS-font drawing step
  auto const data{file(name)};
  return {reinterpret_cast<char const *>(data.data()), data.size()};
}
std::array<uint8_t, 4096> collection::menu_font() const {
  /// MOAG installs the sixteen MEDAL.FNC replacements into its BIOS 8-by-16 font
  std::array<uint8_t, 4096> result;
  auto const base{file("font16.bin")};
  auto const medals{file("medal.fnc")};
  if(base.size() != result.size() || medals.size() % 17 != 0) throw std::invalid_argument{"invalid original menu font"};
  std::ranges::copy(base, result.begin());
  for(unsigned int offset{0}; offset != medals.size(); offset += 17) {
    std::copy_n(medals.begin() + offset, 16, result.begin() + medals[offset + 16] * 16);
  }
  return result;
}
indexed_image collection::image(std::string_view const name) const {
  /// JVCC stores consecutive pixels followed by alignment padding, two extra bytes and an optional six-bit RGB palette
  auto const source{file(name)};
  binary_reader input{source};
  unsigned int const width{input.word(0) + 1u};
  unsigned int const height{input.word(2) + 1u};
  if(width > 640 || height > 350) throw std::invalid_argument{"original artwork dimensions exceed the framebuffer"};
  unsigned int const storage{((width + 7) / 8 * 8) * height};
  bool const embedded_palette{source.size() == 6 + storage + 768};
  if(!embedded_palette && source.size() != 6 + storage) throw std::invalid_argument{"invalid original VGA artwork length"};
  indexed_image result{
    .width{width},
    .height{height},
    .pixels{source.begin() + 4, source.begin() + 4 + width * height},
    .palette{},
  };
  auto const colours{embedded_palette ? source.last(768) : file("vga/hc6.pal")};
  if(colours.size() != 768) throw std::invalid_argument{"invalid original VGA palette length"};
  auto component{[](uint8_t const v){return static_cast<uint8_t>((v << 2) | (v >> 4));}};
  for(unsigned int colour{0}; colour != result.palette.size(); ++colour) {
    result.palette[colour] = {
      .red{component(colours[colour * 3])},
      .green{component(colours[colour * 3 + 1])},
      .blue{component(colours[colour * 3 + 2])},
    };
  }
  return result;
}
std::vector<presentation_panel> collection::story() const {
  /// TPICT.DAT contains the two original story compositions and an explicit minus-one terminator
  std::istringstream input{std::string{text("tpict.dat")}};
  std::vector<presentation_panel> result;
  while(true) {
    int const x{number(input)};
    if(x == -1) break;
    presentation_panel panel{
      .image{},
      .x{x},
      .y{number(input)},
      .text_colour{0},
      .lines{},
    };
    panel.image = filename(input);
    int const lines{number(input)};
    panel.text_colour = static_cast<unsigned int>(number(input));
    for(int n{0}; n != lines; ++n) {
      unsigned int const column{static_cast<unsigned int>(number(input))};
      unsigned int const row{static_cast<unsigned int>(number(input))};
      panel.lines.push_back({
        .row{row},
        .column{column},
        .text{line(input)},
      });
    }
    result.push_back(std::move(panel));
  }
  return result;
}
std::vector<presentation_panel> collection::outcomes() const {
  /// PICT.DAT separates original outcome compositions with form feeds
  std::istringstream scripts{std::string{text("pict.dat")}};
  std::vector<presentation_panel> result;
  std::string block;
  while(std::getline(scripts, block, '\f')) {
    auto const begin{block.find_first_not_of("\r\n \t")};
    if(begin == std::string::npos) continue;
    std::istringstream input{block.substr(begin)};
    int const lines{number(input)};
    int const x{number(input)};
    int const y{number(input)};
    presentation_panel panel{
      .image{filename(input)},
      .x{x},
      .y{y},
      .text_colour{static_cast<unsigned int>(number(input))},
      .lines{},
    };
    for(int n{0}; n != lines; ++n) {
      unsigned int const row{static_cast<unsigned int>(number(input))};
      unsigned int const column{static_cast<unsigned int>(number(input))};
      panel.lines.push_back({
        .row{row},
        .column{column},
        .text{line(input)},
      });
    }
    result.push_back(std::move(panel));
  }
  return result;
}

} // namespace corncob::resources

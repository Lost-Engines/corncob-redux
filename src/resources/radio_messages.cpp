#include "radio_messages.h"
#include <array>
#include <stdexcept>
#include <utility>
#include "resources/assets.h"
#include "resources/binary_reader.h"

namespace corncob::resources {

radio_message read_radio_message(radio_notice const notice) {
  /// Decode original BLACKBOX records directly, preserving their byte-column boxes, BIOS cursor positions and complete text
  std::array<unsigned int, std::to_underlying(radio_notice::count)> constexpr addresses{
    0x0f83, 0x0fdc, 0x102f, 0x1127, 0x1249, 0x129f, 0x12ed, 0x14ae, 0x15f8, 0x174d, 0x189d,
  };
  auto const source{engine_data_asset()};
  binary_reader const data{source};
  unsigned int offset{addresses.at(std::to_underlying(notice))};
  auto box{[&]{
    message_box const result{
      .left{data.word(offset) * 8},
      .top{data.word(offset + 2)},
      .width{data.word(offset + 4) * 8},
      .height{data.word(offset + 6)},
    };
    offset += 8;
    return result;
  }};
  radio_message result{
    .border{box()},
    .background{box()},
    .lines{},
  };
  while(data.byte(offset) != 255) {
    message_line line{
      .column{data.byte(offset)},
      .row{data.byte(offset + 1)},
      .text{},
    };
    offset += 2;
    while(data.byte(offset) != 0) {
      if(line.text.size() >= 80) throw std::invalid_argument{"original radio message exceeds its BIOS line width"};
      line.text += static_cast<char>(data.byte(offset++));
    }
    ++offset;
    result.lines.push_back(std::move(line));
    if(result.lines.size() > 25) throw std::invalid_argument{"original radio message has too many lines"};
  }
  return result;
}

} // namespace corncob::resources

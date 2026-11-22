#pragma once

#include <array>
#include <chrono>
#include <ctime>
#include <format>
#include <string>
#include "maths/integer.h"
#include "game/pilots.h"

namespace corncob::game {

inline int32_t report_time() {
  return maths::dword(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}
inline std::string release_date(int32_t const timestamp) {
  /// Borland's ctime presentation is English local time with a space-padded day and no trailing newline
  std::time_t const time{timestamp};
  auto const *date{std::localtime(&time)};
  if(!date) return "unknown";
  std::array<std::string_view, 7> constexpr days{"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  std::array<std::string_view, 12> constexpr months{"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  return std::format("{} {} {:2} {:02}:{:02}:{:02} {}", days[date->tm_wday], months[date->tm_mon], date->tm_mday, date->tm_hour, date->tm_min, date->tm_sec, date->tm_year + 1900);
}
inline std::string duty_description(pilot_record const *const pilot) {
  if(!pilot) return "ACTIVE";
  switch(pilot->condition(report_time())) {
  case pilot_condition::captured:
    return "CAPTURED";
  case pilot_condition::killed:
    return "K.I.A.";
  case pilot_condition::stockade:
    return "CONFINED TO STOCKADE";
  default:
    return pilot->selected() ? "ACTIVE" : "WAITING FOR DUTY";
  }
}
inline std::string sortie_description(sortie_statistics const &result) {
  std::string text;
  for(auto const &[flag, label] : std::array{
    std::pair{sortie_status::aborted, "MISSION-ABORTED  "},
    std::pair{sortie_status::killed, "KILLED  "},
    std::pair{sortie_status::captured, "CAPTURED  "},
    std::pair{sortie_status::home, "AT-HOME  "},
    std::pair{sortie_status::rescued, "RESCUED  "},
    std::pair{sortie_status::crashed_away, "CRASHED-NOT-HOME  "},
    std::pair{sortie_status::ejected, "EJECTED  "},
    std::pair{sortie_status::crashed_home, "CRASHED-AT-HOME  "},
    std::pair{sortie_status::landed, "LANDED-AT-HOME  "},
    std::pair{sortie_status::building_impact, "COLLISION  "},
  }) {
    if(result.has(flag)) text += label;
  }
  return text;
}

} // namespace corncob::game

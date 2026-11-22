#pragma once

#include <array>
#include <span>
#include <string_view>
#include "world/world.h"

namespace corncob::world {

enum class campaign_id { earth, other_worlds };
enum class theatre_group { earth, other_worlds, practice };

struct theatre_definition {
  std::string_view name;
  std::string_view title;
  std::string_view dos_name;
  unsigned int reserve_aircraft;
  campaign_id campaign{campaign_id::earth};
  bool required{true};
};

/// The four base-game definitions and initial reserve counts retained in MOAG.CFG's original definition table
inline std::array<theatre_definition, 4> constexpr theatre_catalog{
  theatre_definition{
    .name{"morning"},
    .title{"Atkinsville (easy)"},
    .dos_name{"morning"},
    .reserve_aircraft{7},
  },
  theatre_definition{
    .name{"dusk"},
    .title{"Dusk (medium)"},
    .dos_name{"dusk"},
    .reserve_aircraft{6},
  },
  theatre_definition{
    .name{"wasteland"},
    .title{"Wasteland (medium)"},
    .dos_name{"wastelnd"},
    .reserve_aircraft{6},
  },
  theatre_definition{
    .name{"white_sands"},
    .title{"White Sands Testing Ground"},
    .dos_name{"whtsands"},
    .reserve_aircraft{6},
    .required{false},
  },
};

inline std::array<theatre_definition, 13> constexpr other_worlds_catalog{
  theatre_definition{
    .name{"other_worlds/milkrun"},
    .title{"Milk Runs (Easy)"},
    .dos_name{"milkrun"},
    .reserve_aircraft{9},
    .campaign{campaign_id::other_worlds},
    .required{true},
  },
  theatre_definition{
    .name{"other_worlds/flatland"},
    .title{"Flatland (Med Hard)"},
    .dos_name{"flatland"},
    .reserve_aircraft{5},
    .campaign{campaign_id::other_worlds},
    .required{true},
  },
  theatre_definition{
    .name{"other_worlds/quari"},
    .title{"Quari ( Med )"},
    .dos_name{"quari"},
    .reserve_aircraft{9},
    .campaign{campaign_id::other_worlds},
    .required{true},
  },
  theatre_definition{
    .name{"other_worlds/iceworld"},
    .title{"Iceworld ( Hard )"},
    .dos_name{"iceworld"},
    .reserve_aircraft{5},
    .campaign{campaign_id::other_worlds},
    .required{true},
  },
  theatre_definition{
    .name{"other_worlds/midnight"},
    .title{"Midnight"},
    .dos_name{"midnight"},
    .reserve_aircraft{2},
    .campaign{campaign_id::other_worlds},
    .required{true},
  },
  theatre_definition{
    .name{"other_worlds/tundra"},
    .title{"Tundra (medium)"},
    .dos_name{"tundra"},
    .reserve_aircraft{5},
    .campaign{campaign_id::other_worlds},
    .required{false},
  },
  theatre_definition{
    .name{"other_worlds/newatkin"},
    .title{"New Atkinsville (easy)"},
    .dos_name{"newatkin"},
    .reserve_aircraft{1},
    .campaign{campaign_id::other_worlds},
    .required{true},
  },
  theatre_definition{
    .name{"other_worlds/thin"},
    .title{"Breathless  (easy)"},
    .dos_name{"thin"},
    .reserve_aircraft{1},
    .campaign{campaign_id::other_worlds},
    .required{true},
  },
  theatre_definition{
    .name{"other_worlds/exp"},
    .title{"Arrakis  (medium)"},
    .dos_name{"exp"},
    .reserve_aircraft{0},
    .campaign{campaign_id::other_worlds},
    .required{true},
  },
  theatre_definition{
    .name{"other_worlds/morning"},
    .title{"Atkinsville (easy)"},
    .dos_name{"morning"},
    .reserve_aircraft{0},
    .campaign{campaign_id::other_worlds},
    .required{false},
  },
  theatre_definition{
    .name{"other_worlds/dusk"},
    .title{"Dusk (medium)"},
    .dos_name{"dusk"},
    .reserve_aircraft{7},
    .campaign{campaign_id::other_worlds},
    .required{true},
  },
  theatre_definition{
    .name{"other_worlds/wastelnd"},
    .title{"Wasteland (medium)"},
    .dos_name{"wastelnd"},
    .reserve_aircraft{7},
    .campaign{campaign_id::other_worlds},
    .required{true},
  },
  theatre_definition{
    .name{"other_worlds/whtsands"},
    .title{"White Sands Testing Ground"},
    .dos_name{"whtsands"},
    .reserve_aircraft{0},
    .campaign{campaign_id::other_worlds},
    .required{false},
  },
};

inline std::array<theatre_definition, 3> constexpr practice_catalog{
  theatre_definition{
    .name{"other_worlds/default"},
    .title{"Mission builder: empty airfields"},
    .dos_name{"default"},
    .reserve_aircraft{0},
    .campaign{campaign_id::other_worlds},
    .required{false},
  },
  theatre_definition{
    .name{"other_worlds/demo1"},
    .title{"Basic demonstration world"},
    .dos_name{"demo1"},
    .reserve_aircraft{0},
    .campaign{campaign_id::other_worlds},
    .required{false},
  },
  theatre_definition{
    .name{"other_worlds/mtuniv"},
    .title{"empty universe"},
    .dos_name{"mtuniv"},
    .reserve_aircraft{0},
    .campaign{campaign_id::other_worlds},
    .required{false},
  },
};

std::span<theatre_definition const> theatres(theatre_group group) noexcept;
std::span<theatre_definition const> campaign_theatres(campaign_id campaign) noexcept;
theatre_definition const &find_theatre(std::string_view name);
theatre_group group_for(std::string_view name);
theatre load_theatre(std::string_view name);
std::span<uint8_t const> theatre_briefing(std::string_view name);

} // namespace corncob::world

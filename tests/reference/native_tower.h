#pragma once

#include <array>
#include <cstdint>
#include "graphics/raster.h"

namespace corncob::tests {

inline graphics::clip_rectangle constexpr native_tower_crop{
  .left{200},
  .top{4},
  .right{440},
  .bottom{345},
};

struct native_tower_view {
  int32_t x;
  int32_t y;
  uint16_t heading;
  bool doorway;
  uint64_t hash;
};

inline std::array<native_tower_view, 16> constexpr native_tower_views{
  native_tower_view{
    .x{20000},
    .y{0},
    .heading{32768},
    .doorway{false},
    .hash{0xbe431e05c4bc4cbcull},
  },
  native_tower_view{
    .x{20000},
    .y{0},
    .heading{32768},
    .doorway{true},
    .hash{0x52537d24eb0c6161ull},
  },
  native_tower_view{
    .x{14142},
    .y{14142},
    .heading{40960},
    .doorway{false},
    .hash{0x14d6a82cc98f0c14ull},
  },
  native_tower_view{
    .x{14142},
    .y{14142},
    .heading{40960},
    .doorway{true},
    .hash{0x19b88d6023270c74ull},
  },
  native_tower_view{
    .x{0},
    .y{20000},
    .heading{49152},
    .doorway{false},
    .hash{0x0846564f459ac7e5ull},
  },
  native_tower_view{
    .x{0},
    .y{20000},
    .heading{49152},
    .doorway{true},
    .hash{0xfc0b90bb0bd97455ull},
  },
  native_tower_view{
    .x{-14142},
    .y{14142},
    .heading{57344},
    .doorway{false},
    .hash{0x14d6a82cc98f0c14ull},
  },
  native_tower_view{
    .x{-14142},
    .y{14142},
    .heading{57344},
    .doorway{true},
    .hash{0x68c98dbb536f0e68ull},
  },
  native_tower_view{
    .x{-20000},
    .y{0},
    .heading{0},
    .doorway{false},
    .hash{0x0846564f459ac7e5ull},
  },
  native_tower_view{
    .x{-20000},
    .y{0},
    .heading{0},
    .doorway{true},
    .hash{0x3310cc57197de0caull},
  },
  native_tower_view{
    .x{-14142},
    .y{-14142},
    .heading{8192},
    .doorway{false},
    .hash{0x14d6a82cc98f0c14ull},
  },
  native_tower_view{
    .x{-14142},
    .y{-14142},
    .heading{8192},
    .doorway{true},
    .hash{0x2553b0f331535594ull},
  },
  native_tower_view{
    .x{0},
    .y{-20000},
    .heading{16384},
    .doorway{false},
    .hash{0xbe431e05c4bc4cbcull},
  },
  native_tower_view{
    .x{0},
    .y{-20000},
    .heading{16384},
    .doorway{true},
    .hash{0x941cfd87baedd77cull},
  },
  native_tower_view{
    .x{14142},
    .y{-14142},
    .heading{24576},
    .doorway{false},
    .hash{0x14d6a82cc98f0c14ull},
  },
  native_tower_view{
    .x{14142},
    .y{-14142},
    .heading{24576},
    .doorway{true},
    .hash{0x98e4b6d9dca2c594ull},
  },
};

} // namespace corncob::tests

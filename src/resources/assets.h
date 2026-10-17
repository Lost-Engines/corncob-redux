#pragma once

#include <cstdint>
#include <span>

namespace corncob::resources {

std::span<uint8_t const> documentation_asset() noexcept;
std::span<uint8_t const> front_end_asset() noexcept;
std::span<uint8_t const> other_worlds_asset() noexcept;
std::span<uint8_t const> cockpit_asset() noexcept;
std::span<uint8_t const> title_music_asset() noexcept;
std::span<uint8_t const> riding_music_asset() noexcept;
std::span<uint8_t const> dreams_music_asset() noexcept;
std::span<uint8_t const> voyage_music_asset() noexcept;
std::span<uint8_t const> heavens_music_asset() noexcept;
std::span<uint8_t const> daybreak_music_asset() noexcept;
std::span<uint8_t const> engine_data_asset() noexcept;
std::span<uint8_t const> white_sands_asset() noexcept;
std::span<uint8_t const> morning_asset() noexcept;
std::span<uint8_t const> dusk_asset() noexcept;
std::span<uint8_t const> wasteland_asset() noexcept;

std::span<uint8_t const> white_sands_briefing_asset() noexcept;
std::span<uint8_t const> morning_briefing_asset() noexcept;
std::span<uint8_t const> dusk_briefing_asset() noexcept;
std::span<uint8_t const> wasteland_briefing_asset() noexcept;

} // namespace corncob::resources

#pragma once

#include <span>
#include <string_view>
#include "world/world.h"

namespace corncob::resources {

std::string_view documentation_text(bool expansion, std::string_view name);
std::span<uint8_t const> documentation_pdf(std::string_view name);

} // namespace corncob::resources

#include "documentation.h"
#include <string>
#include "resources/assets.h"
#include "resources/collection.h"

namespace corncob::resources {

namespace {

collection const &documents() {
  static collection const result{documentation_asset()};
  return result;
}

} // anonymous namespace

std::string_view documentation_text(bool const expansion, std::string_view const name) {
  if(name == "corncob.doc" || name == "moag.inf") return documents().text(std::string{expansion ? "other_worlds/" : "earth/"} + std::string{name});
  return documents().text(name);
}
std::span<uint8_t const> documentation_pdf(std::string_view const name) {
  return documents().file(std::string{name} + ".pdf");
}

} // namespace corncob::resources

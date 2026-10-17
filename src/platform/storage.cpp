#include "storage.h"
#include <cstdlib>
#include <fstream>
#include <stdexcept>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif // NOMINMAX
#include <windows.h>
#endif // _WIN32

namespace corncob::platform {

std::filesystem::path default_save_directory() {
  /// Keep native user saves separate from embedded installation resources
#ifdef _WIN32
  if(auto const *path{std::getenv("APPDATA")}) return std::filesystem::path{path} / "CorncobRedux";
#elif defined(__APPLE__)
  if(auto const *path{std::getenv("HOME")}) return std::filesystem::path{path} / "Library/Application Support/CorncobRedux";
#else
  if(auto const *path{std::getenv("XDG_DATA_HOME")}; path != nullptr && *path != '\0') return std::filesystem::path{path} / "corncob-redux";
  if(auto const *path{std::getenv("HOME")}) return std::filesystem::path{path} / ".local/share/corncob-redux";
#endif // _WIN32
  return "corncob-redux-saves";
}
std::vector<uint8_t> read_binary(std::filesystem::path const &path) {
  /// Read a complete native save with a contextual error rather than silently replacing an unreadable file
  std::ifstream input{path, std::ios::binary | std::ios::ate};
  if(!input) throw std::runtime_error{"cannot open save file: " + path.string()};
  auto const bytes{input.tellg()};
  if(bytes < 0 || bytes > 64 * 1024 * 1024) throw std::runtime_error{"invalid save-file size: " + path.string()};
  std::vector<uint8_t> result(static_cast<size_t>(bytes));
  input.seekg(0);
  if(!input.read(reinterpret_cast<char*>(result.data()), bytes)) throw std::runtime_error{"cannot read complete save file: " + path.string()};
  return result;
}
void write_binary(std::filesystem::path const &path, std::span<uint8_t const> const bytes) {
  /// Replace a save only after its complete new contents have been written and closed
  std::filesystem::create_directories(path.parent_path());
  auto temporary{path};
  temporary += ".tmp";
  {
    std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
    if(!output || !output.write(reinterpret_cast<char const*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
      throw std::runtime_error{"cannot write save file: " + temporary.string()};
    output.close();
    if(!output) throw std::runtime_error{"cannot finish writing save file: " + temporary.string()};
  }
#ifdef _WIN32
  if(!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw std::runtime_error{"cannot replace save file: " + path.string()};
#else
  std::filesystem::rename(temporary, path);
#endif // _WIN32
}

} // namespace corncob::platform

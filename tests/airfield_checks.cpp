#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include "resources/airfield_survey.h"
#include "resources/assets.h"
#include "resources/binary_reader.h"

int main(int const argc, char const *const argv[]) {
  if(argc != 2) throw std::invalid_argument{"airfield_checks requires its original MOAG fixture"};
  std::ifstream input{argv[1], std::ios::binary};
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>{input}, {}};
  corncob::resources::binary_reader const data{bytes};
  unsigned int offset{16};
  auto entry{[&]{
    corncob::resources::survey_entry result{
      .position{data.signed_dword(offset), data.signed_dword(offset + 4), data.signed_dword(offset + 8)},
      .type{static_cast<corncob::world::object_type>(data.word(offset + 12))},
      .flags{data.word(offset + 14)},
      .group{data.byte(offset + 16)},
    };
    offset += 17;
    return result;
  }};
  unsigned int failures{0};
  for(unsigned int trial{0}; trial != data.dword(8); ++trial) {
    auto const object{entry()};
    corncob::maths::world_position const origin{data.signed_dword(offset), data.signed_dword(offset + 4), 0};
    unsigned int const length{data.word(offset + 8)};
    offset += 10;
    std::string const expected{reinterpret_cast<char const*>(bytes.data() + offset), length};
    offset += length;
    auto const actual{corncob::resources::describe_survey_entry(object, origin)};
    if(actual != expected) {
      if(failures < 10) std::cerr << "Description " << trial << " type " << std::to_underlying(object.type) << " flags " << object.flags << "\nExpected: " << expected << "Actual: " << actual << '\n';
      ++failures;
    }
  }
  for(unsigned int trial{0}; trial != data.dword(12); ++trial) {
    unsigned int const count{data.word(offset)};
    offset += 2;
    std::vector<corncob::resources::survey_entry> objects;
    for(unsigned int n{0}; n != count; ++n) objects.push_back(entry());
    corncob::resources::group_survey_entries(objects);
    for(auto const &actual : objects) {
      auto const expected{entry()};
      if(actual.position.x != expected.position.x || actual.position.y != expected.position.y || actual.position.z != expected.position.z
        || actual.type != expected.type || actual.flags != expected.flags || actual.group != expected.group) {
        if(trial < 2) std::cerr << "Grouping/order " << trial << " expected type " << std::to_underlying(expected.type) << " group " << +expected.group << " pos " << expected.position.x << ',' << expected.position.y << " actual type " << std::to_underlying(actual.type) << " group " << +actual.group << " pos " << actual.position.x << ',' << actual.position.y << '\n';
        ++failures;
      }
    }
  }
  auto const world{corncob::world::decode_theatre(corncob::resources::morning_asset())};
  auto const lines{corncob::resources::airfield_survey(world)};
  std::string actual;
  for(auto const &line : lines) actual += line + "\r\n";
  std::ifstream full_reference{std::filesystem::path{argv[1]}.parent_path() / "airfield-morning.inf", std::ios::binary};
  std::string const expected{std::istreambuf_iterator<char>{full_reference}, {}};
  if(actual != expected) {
    std::cerr << "Complete morning survey differs from original TOWERS.INF\n";
    ++failures;
  }
  if(offset != bytes.size()) throw std::runtime_error{"native airfield fixture has an unread trailer"};
  if(failures != 0) { std::cerr << failures << " survey differences\n"; return EXIT_FAILURE; }
  std::cout << data.dword(8) << " original survey descriptions and " << data.dword(12) << " complete grouping/sort cases match" << std::endl;
}

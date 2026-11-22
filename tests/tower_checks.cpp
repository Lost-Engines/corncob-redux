#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include "game/front_end.h"
#include "resources/assets.h"
#include "tests/reference/tower_briefings.h"

int main(int const argc, char const *const argv[]) {
  /// Compare full screens and selected start coordinates with unchanged TOWER.EXE, including cancellation and every airfield section
  if(argc < 2 || argc > 3) throw std::invalid_argument{"tower_checks requires its scratch directory and an optional capture directory"};
  std::filesystem::path const scratch{argv[1]};
  if(scratch.filename() != "tower-checks") throw std::invalid_argument{"tower scratch directory must be named tower-checks"};
  std::filesystem::remove_all(scratch);
  namespace game = corncob::game;
  using enum game::interface_command;
  corncob::resources::collection const assets{corncob::resources::front_end_asset()};
  game::campaign_store career{scratch};
  auto const storage{std::make_unique<game::front_end>(assets, career, true)};
  auto &front{*storage};
  auto key{[&](std::string_view const name){
    if(name == "Up") front.command(up);
    else if(name == "Down") front.command(down);
    else if(name == "Left") front.command(previous);
    else if(name == "Right") front.command(next);
    else if(name == "Prior") front.command(page_up);
    else if(name == "Next") front.command(page_down);
    else if(name == "Home") front.command(home);
    else if(name == "End") front.command(end);
    else if(name == "Return") front.command(accept);
    else if(name == "Escape") front.command(cancel);
    else if(name == "BackSpace") front.command(backspace);
    else if(name == "F1") front.show_boss();
    else if(name == "space") {
      if(front.current_page() == game::interface_page::boss) front.command(accept);
      else front.character(' ');
    } else if(name.starts_with("ctrl+") && name.size() == 6) front.character(static_cast<uint32_t>(name.back() - 'a' + 1));
    else if(name.size() == 1) front.character(static_cast<unsigned char>(name.front()));
    else throw std::runtime_error{"unknown native tower input"};
  }};
  if(argc == 3) std::filesystem::create_directories(argv[2]);
  unsigned int failures{0};
  for(auto const &reference : corncob::tests::tower_cases) {
    corncob::world::theatre world;
    auto const raw{reference.source == "MORNING" ? corncob::resources::morning_briefing_asset()
      : reference.source == "DUSK" ? corncob::resources::dusk_briefing_asset()
      : reference.source == "WASTELND" ? corncob::resources::wasteland_briefing_asset()
      : reference.source == "WHTSANDS" ? corncob::resources::white_sands_briefing_asset() : std::span<uint8_t const>{}};
    world.briefing = raw.empty() ? std::string{reference.text} : std::string{reinterpret_cast<char const*>(raw.data()), raw.size()};
    if(reference.source == "TRAINING") {
      corncob::resources::geometry_bank const geometry{corncob::resources::engine_data_asset()};
      world = corncob::world::generate_training(geometry, {});
    }
    front.show_intelligence(world, reference.airfield);
    std::istringstream keys{std::string{reference.keys}};
    std::string input;
    while(keys >> input) key(input);
    corncob::render::framebuffer frame;
    front.render(frame);
    uint64_t hash{0xcbf29ce484222325ull};
    for(auto const &pixel : frame.pixels) {
      for(auto const component : {pixel.red, pixel.green, pixel.blue}) hash = (hash ^ component) * 0x100000001b3ull;
    }
    if(frame.width != 640 || frame.height != 400 || hash != reference.rgb_hash) {
      std::cerr << reference.name << " RGB differs: " << std::hex << hash << " expected " << reference.rgb_hash << std::dec << '\n';
      ++failures;
    }
    if(argc == 3) {
      std::ofstream file{std::filesystem::path{argv[2]} / (std::string{reference.name} + ".ppm"), std::ios::binary};
      file << "P6\n640 400\n255\n";
      for(auto const &pixel : frame.pixels) for(auto const component : {pixel.red, pixel.green, pixel.blue}) file.put(static_cast<char>(component));
    }
    key(reference.close);
    if(!front.in_flight()) throw std::runtime_error{"tower helper did not resume the retained flight"};
    auto const start{front.take_relocation()};
    if(start.has_value() != !reference.start.empty()) throw std::runtime_error{"tower selection presence differs: " + std::string{reference.name}};
    if(start) {
      std::istringstream expected{std::string{reference.start}};
      int32_t x{0}, y{0}, z{0}, heading{0};
      if(!(expected >> x >> y >> z >> heading) || start->position.x != x || start->position.y != y || start->position.z != z
          || start->heading != static_cast<corncob::maths::angle>(heading)) throw std::runtime_error{"native tower mission coordinates differ: " + std::string{reference.name}};
    }
    if(front.take_relocation() || front.music() != corncob::audio::music_cue::silent) throw std::runtime_error{"tower repeated a start or enabled menu music"};
  }
  /// The helper's hidden F1 boss screen restores the same scroll position and selected mission when dismissed
  corncob::world::theatre world;
  corncob::world::apply_briefing(world, corncob::resources::morning_briefing_asset());
  front.show_intelligence(world);
  front.command(page_down);
  corncob::render::framebuffer before, after;
  front.render(before);
  front.show_boss();
  front.command(accept);
  front.render(after);
  if(front.current_page() != game::interface_page::briefing || !std::ranges::equal(before.pixels, after.pixels, [](auto const &a, auto const &b){return a.red == b.red && a.green == b.green && a.blue == b.blue;})) throw std::runtime_error{"tower boss screen lost its retained briefing"};
  front.command(cancel);
  if(front.take_relocation()) throw std::runtime_error{"cancelling after boss selected a mission"};
  /// A new helper invocation clears an unconsumed earlier selection, just as deleting STARTLOC.DAT does in DOS
  front.show_intelligence(world);
  front.command(accept);
  front.show_intelligence(world);
  front.command(cancel);
  if(front.take_relocation()) throw std::runtime_error{"tower retained an earlier invocation's mission start"};
  try {
    game::tower_briefing invalid{world.briefing, game::tower_briefing::airfield_count};
    throw std::runtime_error{"tower accepted an out-of-range airfield"};
  } catch(std::out_of_range const&) {
  }
  std::filesystem::remove_all(scratch);
  if(failures != 0) return EXIT_FAILURE;
  std::cout << corncob::tests::tower_cases.size() << " unchanged DOS tower screens and mission handoffs match; boss restores the briefing" << std::endl;
  return EXIT_SUCCESS;
}

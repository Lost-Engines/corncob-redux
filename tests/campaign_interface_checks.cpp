#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include "platform/storage.h"
#include "game/front_end.h"
#include "game/report_format.h"
#include "resources/assets.h"

int main(int const argc, char const *const argv[]) {
  if(argc < 3 || argc > 4) throw std::invalid_argument{"campaign_interface_checks requires references, scratch and optional captures"};
  std::filesystem::path const scratch{argv[2]};
  if(scratch.filename() != "campaign-interface-checks") throw std::invalid_argument{"unexpected campaign test scratch directory"};
  std::filesystem::remove_all(scratch);
  namespace game = corncob::game;
  namespace world = corncob::world;
  namespace resources = corncob::resources;
  using enum game::interface_command;
  std::map<std::string, uint64_t> hashes;
  std::ifstream references{argv[1]};
  std::string name;
  uint64_t expected{0};
  while(references >> name >> std::hex >> expected) hashes.emplace(name, expected);
  resources::collection const assets{resources::front_end_asset()};
  resources::geometry_bank const geometry{resources::engine_data_asset()};
  auto const pristine{world::decode_theatre(resources::morning_asset())};
  game::campaign_store campaign{scratch};
  campaign.keyboard_control(true);
  auto storage{std::make_unique<game::front_end>(assets, campaign, true)};
  auto &initial{*storage};
  corncob::render::framebuffer frame;
  unsigned int failures{0}, compared{0};
  if(hashes.size() != 11) throw std::runtime_error{"missing native campaign screen references"};
  auto check{[&](game::front_end const &front, std::string const &label){
    front.render(frame);
    uint64_t hash{0xcbf29ce484222325ull};
    for(auto const &pixel : frame.pixels) for(auto const value : {pixel.red, pixel.green, pixel.blue}) hash = (hash ^ value) * 0x100000001b3ull;
    if(auto const found{hashes.find(label)}; found != hashes.end()) {
      ++compared;
      if(found->second != hash) {
      std::cerr << label << ": original " << std::hex << found->second << ", actual " << hash << std::dec << '\n';
      ++failures;
      }
    }
    if(argc == 4) {
      std::filesystem::create_directories(argv[3]);
      std::ofstream output{std::filesystem::path{argv[3]} / (label + ".ppm"), std::ios::binary};
      output << "P6\n640 400\n255\n";
      for(auto const &pixel : frame.pixels) { output.put(static_cast<char>(pixel.red)); output.put(static_cast<char>(pixel.green)); output.put(static_cast<char>(pixel.blue)); }
    }
  }};
  game::sortie_statistics live{
    .resource_kills{3},
    .objectives_completed{1},
    .elapsed_seconds{125},
  };
  initial.show_current_score(live);
  check(initial, "preliminary");
  if(!campaign.pilots().empty()) throw std::runtime_error{"preliminary report persisted a trainee career"};
  initial.command(accept);
  initial.finish_sortie({}); initial.command(accept); initial.command(accept); initial.command(accept);
  initial.character('d'); initial.character('c');
  for(unsigned int c : std::string{"Gap Pilot"}) initial.character(c);
  initial.command(accept); initial.command(accept); initial.command(cancel);
  initial.character('c'); initial.character('o'); initial.command(accept); initial.character('f');
  check(initial, "starting");
  initial.character('i'); check(initial, "survey");
  initial.command(end); check(initial, "survey-end");
  initial.command(cancel); initial.command(cancel);

  auto empty{world::theatre{pristine}};
  for(auto &object : empty.objects) if(object.type == world::object_type::aircraft) object.flags |= 0x100;
  game::sortie_statistics ignored;
  ignored.mark(game::sortie_status::invincible);
  campaign.finish_sortie("morning", empty, ignored);
  auto empty_front{std::make_unique<game::front_end>(assets, campaign, true)};
  empty_front->character('c'); empty_front->character('f'); empty_front->command(accept);
  if(empty_front->current_page() != game::interface_page::no_aircraft) throw std::runtime_error{"empty airfield did not offer a reserve"};
  check(*empty_front, "reserve");
  unsigned int const reserves{campaign.theatres().front().stored_aircraft};
  empty_front->character('p');
  auto const launch{empty_front->take_launch()};
  if(!launch || !launch->reserve_aircraft || campaign.theatres().front().stored_aircraft != reserves) throw std::runtime_error{"reserve launch consumed stock prematurely"};
  auto sortie{std::make_unique<game::session>(geometry, world::theatre{empty}, true)};
  if(sortie->player() != game::player_mode::in_aircraft || sortie->craft().variant != corncob::flight::aircraft_variant::reserve
    || sortie->craft().position.x != empty.start.x || sortie->craft().position.y != empty.start.y) throw std::runtime_error{"reserve handoff did not board a Dumpy at the saved start"};
  game::sortie_statistics returned;
  returned.mark(game::sortie_status::home); returned.mark(game::sortie_status::landed);
  campaign.finish_sortie("morning", sortie->snapshot(), returned, true);
  if(campaign.theatres().front().stored_aircraft + 1 != reserves) throw std::runtime_error{"returned reserve did not consume one stored plane"};
  game::campaign_store reopened{scratch};
  if(reopened.theatres().front().stored_aircraft + 1 != reserves) throw std::runtime_error{"reserve count did not survive restart"};

  /// Invalid or invulnerable sorties leave storage unchanged; zero stock never accepts P
  unsigned int const stock{reopened.theatres().front().stored_aircraft};
  reopened.finish_sortie("morning", empty, ignored, true);
  if(reopened.theatres().front().stored_aircraft != stock) throw std::runtime_error{"uncounted sortie consumed a reserve"};
  for(unsigned int i{0}; i != stock; ++i) reopened.finish_sortie("morning", empty, returned, true);
  auto exhausted{std::make_unique<game::front_end>(assets, reopened, true)};
  exhausted->character('c'); exhausted->character('f'); exhausted->command(accept);
  check(*exhausted, "no-reserves");
  exhausted->character('p');
  if(exhausted->take_launch()) throw std::runtime_error{"zero stock launched a reserve"};
  exhausted->command(accept); exhausted->command(accept);
  auto const foot_launch{exhausted->take_launch()};
  if(!foot_launch || foot_launch->reserve_aircraft) throw std::runtime_error{"empty field could not launch on foot"};

  campaign.finish_sortie("morning", pristine, ignored);
  auto completed{world::theatre{pristine}};
  for(auto &object : completed.objects) object.flags |= std::to_underlying(world::object_flag::objective_complete);
  campaign.finish_sortie("morning", completed, ignored);
  auto completed_front{std::make_unique<game::front_end>(assets, campaign, true)};
  completed_front->character('c'); completed_front->character('f');
  if(completed_front->current_page() != game::interface_page::no_objectives) throw std::runtime_error{"completed theater did not warn before flight"};
  check(*completed_front, "no-objectives");
  completed_front->command(cancel); completed_front->character('d'); completed_front->command(accept);
  if(completed_front->current_page() != game::interface_page::completed_theatre) throw std::runtime_error{"completed theater deleted without confirmation"};
  check(*completed_front, "completed-deletion");
  completed_front->command(accept);
  if(campaign.theatres().empty()) throw std::runtime_error{"Enter incorrectly confirmed completed-theater deletion"};
  completed_front->character('d'); completed_front->command(accept); completed_front->character('y');
  if(!campaign.theatres().empty()) throw std::runtime_error{"completed-theater deletion was not accepted by Y"};

  /// A populated table exposes alignment, retired markers, separate kill totals and average-score arithmetic
  auto const score_directory{scratch / "scores"};
  game::campaign_store score_setup{score_directory};
  score_setup.keyboard_control(true);
  score_setup.create_pilot("Reserve Pilot");
  score_setup.open_theatre("morning", pristine);
  corncob::platform::write_binary(score_directory / "PILOT.HSC", corncob::platform::read_binary(std::filesystem::path{argv[1]}.parent_path() / "populated_scores.bin"));
  game::campaign_store score_campaign{score_directory};
  auto score_front{std::make_unique<game::front_end>(assets, score_campaign, true)};
  score_front->character('h'); score_front->character('s'); check(*score_front, "populated-scores");
  score_front->command(cancel); score_front->character('c'); check(*score_front, "populated-careers");
  score_front->command(cancel); score_front->character('f'); check(*score_front, "populated-fastest");

  /// A complete return path uses the pre-sortie rank in its mission report, then presents honors and the updated career
  game::campaign_store report_campaign{scratch / "reports"};
  report_campaign.keyboard_control(true);
  report_campaign.create_pilot("Reserve Pilot");
  report_campaign.open_theatre("morning", pristine);
  auto const before_report{*report_campaign.active_pilot()};
  auto const before_theatre{report_campaign.theatres().front().pilot};
  auto report_front{std::make_unique<game::front_end>(assets, report_campaign, true)};
  game::sortie_statistics awarded{
    .resource_kills{400},
    .objectives_completed{20},
    .maximum_damage{150},
  };
  awarded.mark(game::sortie_status::home); awarded.mark(game::sortie_status::landed);
  report_campaign.finish_sortie("morning", completed, awarded);
  report_front->finish_sortie(awarded, &before_report, &before_theatre);
  report_front->command(accept); check(*report_front, "mission-report");
  report_front->command(accept);
  unsigned int honor{0};
  while(report_front->current_page() == game::interface_page::career_announcement && honor < 20) {
    if(honor == 0) check(*report_front, "promotion");
    if(honor == 5) check(*report_front, "decoration");
    ++honor;
    report_front->command(accept);
  }
  if(honor != 6 || report_front->current_page() != game::interface_page::post_mission_report) throw std::runtime_error{"original six-honor report chain changed"};
  check(*report_front, "post-mission");
  report_front->command(accept);

  game::sortie_statistics fatal;
  fatal.mark(game::sortie_status::killed);
  campaign.finish_sortie("training", pristine, fatal);
  auto dead_front{std::make_unique<game::front_end>(assets, campaign, true)};
  dead_front->character('d'); dead_front->character('a'); dead_front->command(accept);
  if(dead_front->current_page() != game::interface_page::unavailable_pilot) throw std::runtime_error{"dead pilot skipped resurrection offer"};
  check(*dead_front, "unavailable");
  dead_front->character('y');
  if(!campaign.can_fly() || campaign.active_pilot()->resurrections() != 1) throw std::runtime_error{"activation did not resurrect the chosen pilot"};
  auto const prior_pilot{*campaign.active_pilot()};
  game::sortie_statistics hero{
    .resource_kills{400},
    .objectives_completed{20},
    .maximum_damage{150},
  };
  hero.mark(game::sortie_status::home); hero.mark(game::sortie_status::landed);
  campaign.finish_sortie("training", pristine, hero);
  dead_front->finish_sortie(hero, &prior_pilot);
  dead_front->command(accept); dead_front->command(accept);
  if(dead_front->current_page() != game::interface_page::career_announcement) throw std::runtime_error{"new career honors were not presented"};
  unsigned int ceremonies{0};
  while(dead_front->current_page() == game::interface_page::career_announcement && ceremonies != 20) {
    check(*dead_front, "ceremony-" + std::to_string(ceremonies++));
    dead_front->command(accept);
  }
  if(ceremonies == 0 || ceremonies == 20 || dead_front->current_page() != game::interface_page::post_mission_report) throw std::runtime_error{"career announcements did not return to the menu"};
  dead_front->command(accept);
  campaign.finish_sortie("training", pristine, fatal);
  campaign.deactivate();
  auto manual{std::make_unique<game::front_end>(assets, campaign, true)};
  manual->character('d'); manual->character('e'); manual->command(accept); manual->command(accept);
  if(manual->current_page() != game::interface_page::resurrect_activate) throw std::runtime_error{"manual resurrection skipped the activation question"};
  check(*manual, "resurrect-activate");
  manual->character('n');
  if(campaign.active_pilot() || campaign.pilots()[0].unavailable()) throw std::runtime_error{"declining activation undid resurrection or selected the pilot"};
  campaign.activate(0);
  game::sortie_statistics captured;
  captured.mark(game::sortie_status::captured);
  campaign.finish_sortie("training", pristine, captured);
  auto captive{std::make_unique<game::front_end>(assets, campaign, true)};
  captive->character('d'); captive->character('a'); captive->command(accept);
  check(*captive, "captured");
  captive->character('n');
  if(!campaign.pilots()[0].unavailable()) throw std::runtime_error{"declining resurrection released a captive"};
  campaign.resurrect(0); campaign.activate(0);
  game::sortie_statistics friendly{
    .friendly_destroyed{1},
  };
  friendly.mark(game::sortie_status::home);
  campaign.finish_sortie("training", pristine, friendly);
  auto prisoner{std::make_unique<game::front_end>(assets, campaign, true)};
  prisoner->character('d'); prisoner->character('a'); prisoner->command(accept);
  if(prisoner->current_page() != game::interface_page::unavailable_pilot) throw std::runtime_error{"stockade did not prevent activation"};
  unsigned int const resurrections{campaign.pilots()[0].resurrections()};
  prisoner->character('y');
  if(campaign.can_fly() || campaign.pilots()[0].resurrections() != resurrections) throw std::runtime_error{"stockade allowed resurrection to bypass the sentence"};

  game::campaign_store trainee{scratch / "trainee"};
  trainee.open_theatre("morning", empty);
  unsigned int const trainee_stock{trainee.theatres().front().stored_aircraft};
  trainee.finish_sortie("morning", empty, returned, true);
  if(trainee.theatres().front().stored_aircraft + 1 != trainee_stock) throw std::runtime_error{"trainee return did not consume its transient reserve"};
  game::campaign_store const next_trainee{scratch / "trainee"};
  if(!next_trainee.theatres().empty()) throw std::runtime_error{"trainee storage persisted as a named campaign"};
  std::filesystem::remove_all(scratch);
  if(compared != hashes.size()) throw std::runtime_error{"not all native campaign screens were checked"};
  if(failures != 0) return EXIT_FAILURE;
  std::cout << "Original preliminary/survey screens, reserve persistence and campaign dialog transitions verified\n";
}

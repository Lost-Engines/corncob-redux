#include "front_end.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <format>
#include <sstream>
#include <utility>
#include <vector>
#include "game/report_format.h"
#include "platform/storage.h"
#include "render/palette.h"
#include "resources/airfield_survey.h"
#include "resources/assets.h"
#include "resources/documentation.h"
#include "world/airfields.h"
#include "world/theatre_catalog.h"
#include "world/world.h"

namespace corncob::game {

namespace {

render::rgba_pixel constexpr white{255, 255, 255}, grey{170, 170, 170}, yellow{255, 255, 85}, blue{0, 0, 170};

} // anonymous namespace

front_end::front_end(resources::collection const &assets, campaign_store &campaign, bool const skip_intro, bool const start_in_flight, std::optional<control_setup> const controls, std::optional<bool> const music_enabled) :
  assets{assets},
  expansion_assets{resources::other_worlds_asset(), &assets},
  campaign{campaign},
  menu_font{assets.menu_font()},
  training{campaign.training_settings()},
  controls{controls.value_or(control_setup{
    .joystick_device{campaign.keyboard_control() ? -2 : -1},
    .beginner_flight{!campaign.active_pilot() || campaign.active_pilot()->beginner_flight()},
  })},
  stories{assets.story()},
  outcomes{campaign_assets().outcomes()},
  music_on{music_enabled.value_or(campaign.music_enabled())} {
  /// One state machine replaces the original title, menu, help and outcome helper processes
  settings = preferences::load(campaign.save_directory() / "redux.cfg");
  page = start_in_flight ? interface_page::flight : skip_intro ? interface_page::menu : interface_page::story;
  group = campaign.current_campaign() == world::campaign_id::earth ? world::theatre_group::earth : world::theatre_group::other_worlds;
  session_running = start_in_flight;
  saved_theatres = campaign.theatres();
  combat_invulnerable = campaign.combat_invincible();
  comments_enabled = campaign.inflight_comments();
  for(auto const name : {"img/3d2.img", "img/3d3.img", "img/3d4.img", "img/3d5.img", "img/3d6.img",
    "img/3d7.img", "img/3d8.img", "img/3d9.img", "img/3d10.img", "img/3d13.img"}) help_screens.push_back(resources::decode_cockpit_mask(assets.file(name)));
}
resources::collection const &front_end::campaign_assets() const noexcept {
  return campaign.current_campaign() == world::campaign_id::earth ? assets : expansion_assets;
}
void front_end::choose_group(world::theatre_group const selected) {
  group = selected;
  campaign.select_campaign(group == world::theatre_group::earth ? world::campaign_id::earth : world::campaign_id::other_worlds);
  sync_pilot();
  outcomes = campaign_assets().outcomes();
  remembered_selection[interface_page::new_theatre] = 0;
}
void front_end::navigate(interface_page const destination) {
  /// Each original menu remembers its highlighted entry when a child screen closes
  remembered_selection[page] = selection;
  page = destination;
  auto const found{remembered_selection.find(page)};
  selection = found == remembered_selection.end() ? (page == interface_page::menu ? 2u : 0u) : found->second;
}
void front_end::sync_pilot() {
  /// Activating a different career restores that pilot's menu preferences
  training = campaign.training_settings();
  controls.beginner_flight = !campaign.active_pilot() || campaign.active_pilot()->beginner_flight();
  saved_theatres = campaign.theatres();
  combat_invulnerable = campaign.combat_invincible();
  comments_enabled = campaign.inflight_comments();
}
std::string_view front_end::document_text(std::string_view const name) const {
  return resources::documentation_text(campaign.current_campaign() == world::campaign_id::other_worlds, name);
}
void front_end::document(std::string_view text, unsigned int const section) {
  /// Form feeds separate the original documentation chapters and context-sensitive menu help
  for(unsigned int n{0}; n < section; ++n) {
    auto const split{text.find('\f')};
    if(split == std::string_view::npos) throw std::invalid_argument{"missing original documentation section"};
    text.remove_prefix(split + 1);
  }
  text = text.substr(0, text.find('\f'));
  std::istringstream input{std::string{text}};
  std::string line;
  document_lines.clear();
  while(std::getline(input, line)) {
    if(!line.empty() && line.back() == '\r') line.pop_back();
    document_lines.push_back(line);
  }
  scroll = 0;
}
void front_end::information(unsigned int const section) {
  /// Menu information is separate from F4's original in-flight artwork
  information_return = page;
  information_section = section;
  document(document_text("moag.inf"), section);
  if(!document_lines.empty()) document_lines.erase(document_lines.begin());
  navigate(interface_page::menu_information);
}
void front_end::notice(std::string message) {
  /// Report a failed operation inside the same menu without discarding its selection or saved state
  information_return = page;
  feedback = std::move(message);
  navigate(interface_page::notice);
}
void front_end::launch_combat() {
  /// A combat flight resumes the active saved theater; selecting a different definition alone does not reset it
  auto const &worlds{saved_theatres};
  auto const active{campaign.active_theatre()};
  if(!active || *active >= worlds.size()) { notice("Sorry, there is no active theater-of-operation.\nOpen or activate a theater before choosing Fly mission."); return; }
  prepared_world = campaign.load_world(worlds[*active].definition);
  starting_airfield = prepared_world ? world::nearest_airfield(*prepared_world, prepared_world->start) : world::central_airfield;
  prepared_launch = launch_request{
    .theatre{worlds[*active].definition},
    .invincible{combat_invulnerable},
    .comments{comments_enabled},
    .airfield{starting_airfield},
  };
  navigate(prepared_world && resources::survey_airfield(*prepared_world, world::airfield_count).objectives == 0
    ? interface_page::no_objectives : interface_page::starting_airfield);
  if(page == interface_page::starting_airfield) selection = starting_airfield;
}
void front_end::finish_reports() {
  if(std::exchange(pending_campaign_victory, false)) {
    navigate(interface_page::campaign_victory);
    return;
  }
  /// MOAG returns an unavailable named pilot to the roster and resumes with the disposable trainee
  if(!campaign.can_fly()) campaign.deactivate();
  sync_pilot();
  navigate(interface_page::menu);
}
void front_end::accept() {
  /// Reproduce MOAG's menu hierarchy while retaining one native session and save store
  try {
    switch(page) {
    case interface_page::radio:
      radio.reset(); page = interface_page::flight; pending_map = std::exchange(map_after_radio, false); break;
    case interface_page::ending: ending->toggle_warp(); break;
    case interface_page::title: title.reset(); navigate(interface_page::menu); break;
    case interface_page::story: advancing_story = true; break;
    case interface_page::menu:
      if(session_running) {
        if(selection == 0) page = interface_page::flight;
        else if(selection == 1) navigate(interface_page::control_setup);
        else if(selection == 2) show_flight_help();
        else ask_to_end_sortie();
      } else switch(selection) {
      case 0: navigate(interface_page::roster); break;
      case 1: navigate(interface_page::theatres); break;
      case 2: navigate(interface_page::training); break;
      case 3: navigate(interface_page::high_score_menu); break;
      case 4: navigate(interface_page::campaigns); break;
      case 5: navigate(interface_page::control_setup); break;
      case 6: music_on = !music_on; campaign.music_enabled(music_on); break;
      case 7: document_chapter = 0; numeric_input.clear(); document(document_text("corncob.doc"), 0); navigate(interface_page::documentation); break;
      case 8:
        {
          auto const directory{std::filesystem::absolute(campaign.save_directory() / "documentation").lexically_normal()};
          for(auto const name : {"earth", "other_worlds", "hints", "mission_builder"}) platform::write_binary(directory / (std::string{name} + ".pdf"), resources::documentation_pdf(name));
          notice("PDF manuals saved to:\n" + directory.string() + "\n\nearth.pdf, other_worlds.pdf, hints.pdf, mission_builder.pdf");
        }
        break;
      case 9: information(1); break;
      case 10: quit_requested = true; break;
      }
      break;
    case interface_page::campaigns:
      if(selection < 3) {
        choose_group(static_cast<world::theatre_group>(selection));
        navigate(interface_page::new_theatre);
      } else if(selection == 3) navigate(interface_page::demonstrations);
      else if(selection == 4) {
        choose_group(world::theatre_group::practice);
        navigate(interface_page::new_theatre);
        selection = 0;
        character('b');
      } else if(selection < 7) {
        auto text{std::string{document_text(selection == 5 ? "mbuilder.doc" : "hints.txt")}};
        std::ranges::replace(text, '\f', '\n');
        document(text, 0);
        information_return = interface_page::campaigns;
        navigate(interface_page::registration);
      } else if(selection == 7) {
        choose_group(world::theatre_group::other_worlds);
        title.emplace(expansion_assets, true);
        page = interface_page::title;
      } else navigate(interface_page::menu);
      break;
    case interface_page::demonstrations:
      if(selection == 3) navigate(interface_page::campaigns);
      else {
        choose_group(world::theatre_group::practice);
        std::array<std::string_view, 3> constexpr recordings{"demo1", "demo5", "demo6"};
        pending_launch = launch_request{
          .theatre{"other_worlds/demo1"},
          .comments{false},
          .recording{recordings[selection]},
        };
      }
      break;
    case interface_page::roster:
      if(selection == 0) { renaming = false; entered_name.clear(); navigate(interface_page::pilot_name); }
      else if(selection == 2) { campaign.deactivate(); sync_pilot(); }
      else if(selection == 7) information(2);
      else if(selection == 8) navigate(interface_page::menu);
      else if(campaign.pilots().empty()) notice("There are no pilots in the list.\nYou will have to create one first.");
      else {
        pilot_action = selection == 1 ? roster_action::activate : selection == 3 ? roster_action::remove
          : selection == 4 ? roster_action::resurrect : selection == 5 ? roster_action::rename : roster_action::records;
        pilot_index = 0;
        if(pilot_action == roster_action::records) { detailed_records = false; navigate(interface_page::records); }
        else { navigate(interface_page::pilots); selection = 0; }
      }
      break;
    case interface_page::pilots:
      pilot_index = selection;
      switch(pilot_action) {
      case roster_action::activate:
        if(campaign.pilots()[pilot_index].unavailable() || campaign.pilots()[pilot_index].suspended_until() > report_time()) navigate(interface_page::unavailable_pilot);
        else { campaign.activate(pilot_index); sync_pilot(); navigate(interface_page::roster); }
        break;
      case roster_action::remove: navigate(interface_page::pilot_delete); break;
      case roster_action::rename: renaming = true; entered_name = campaign.pilots()[pilot_index].name(); navigate(interface_page::pilot_name); break;
      case roster_action::resurrect:
        if(!campaign.pilots()[pilot_index].unavailable()) navigate(interface_page::unavailable_pilot);
        else navigate(interface_page::pilot_resurrect);
        break;
      case roster_action::records: detailed_records = false; navigate(interface_page::records); break;
      }
      break;
    case interface_page::pilot_delete: campaign.delete_pilot(pilot_index); sync_pilot(); navigate(interface_page::roster); break;
    case interface_page::pilot_resurrect:
      campaign.resurrect(pilot_index);
      navigate(campaign.pilots()[pilot_index].selected() ? interface_page::roster : interface_page::resurrect_activate);
      break;
    case interface_page::unavailable_pilot: navigate(interface_page::roster); break;
    case interface_page::resurrect_activate: navigate(interface_page::roster); break;
    case interface_page::pilot_name:
      if(renaming) { campaign.rename_pilot(pilot_index, entered_name); navigate(interface_page::roster); }
      else {
        pilot_record const validated{entered_name};
        if(std::ranges::any_of(campaign.pilots(), [&](auto const &pilot){return pilot.name() == validated.name();}))
          throw std::invalid_argument{"Sorry, that pilot already exists."};
        navigate(interface_page::pilot_activate); selection = 0;
      }
      break;
    case interface_page::pilot_activate: campaign.create_pilot(entered_name, selection == 0); sync_pilot(); navigate(interface_page::roster); break;
    case interface_page::records:
    case interface_page::medals: navigate(interface_page::roster); break;
    case interface_page::boss: page = boss_return; break;
    case interface_page::current_score: page = interface_page::flight; break;
    case interface_page::sortie_summary:
      announcement_index = 0;
      if(announcements.empty()) navigate(interface_page::post_mission_report);
      else navigate(interface_page::career_announcement);
      break;
    case interface_page::campaign_victory:
    case interface_page::post_mission_report: finish_reports(); break;
    case interface_page::career_announcement:
      if(++announcement_index == announcements.size()) navigate(interface_page::post_mission_report);
      break;
    case interface_page::control_setup:
      if(selection == 0) { return_page = interface_page::control_setup; navigate(interface_page::bindings); }
      else if(selection == 14) { save_settings(); navigate(interface_page::menu); }
      else adjust_setting(1);
      break;
    case interface_page::bindings:
      if(selection < settings.bindings.size()) binding_capture = true;
      else if(selection == settings.bindings.size()) { settings.bindings = default_bindings(); save_settings(); }
      else navigate(return_page);
      break;
    case interface_page::training:
      switch(selection) {
      case 0:
        if(!campaign.can_fly()) { notice("This pilot is not available for duty."); break; }
        prepared_launch = launch_request{
          .theatre{"training"},
          .training{training},
          .invincible{training.invincible},
          .comments{comments_enabled},
        };
        navigate(interface_page::mission_briefing);
        break;
      case 1: navigate(interface_page::training_wickedness); break;
      case 2: navigate(interface_page::training_counts); break;
      case 3: training = {}; campaign.training_settings(training); break;
      case 4: training.invincible = !training.invincible; campaign.training_settings(training); break;
      case 5: information(6); break;
      case 6: navigate(interface_page::menu); break;
      }
      break;
    case interface_page::training_wickedness:
    case interface_page::training_counts:
      if(selection == 3) navigate(interface_page::training);
      else { training_editor = page; training_field = selection; numeric_input.clear(); navigate(interface_page::training_value); }
      break;
    case interface_page::training_value:
      if(!numeric_input.empty()) {
        unsigned int const value{static_cast<unsigned int>(std::stoi(numeric_input))};
        bool const quality{training_editor == interface_page::training_wickedness};
        if(value > (quality ? 8u : world::training_defence::maximum_menu_count)) { feedback = quality ? "Enter a value from 0 to 8" : "Enter a value from 0 to 25"; break; }
        auto &defence{training_field == 0 ? training.kla : training_field == 1 ? training.vehicles : training.aaa};
        if(quality) defence.quality = static_cast<int>(value) - 1;
        else defence.count = value;
        campaign.training_settings(training);
      }
      feedback.clear(); navigate(training_editor); break;
    case interface_page::theatres:
      switch(selection) {
      case 0: if(campaign.can_fly()) launch_combat(); else notice("This pilot is not available for duty."); break;
      case 1: navigate(interface_page::new_theatre); break;
      case 2: case 3: case 4:
        if(saved_theatres.empty()) notice("Sorry, this pilot has no open theaters to choose from.");
        else { detailed_records = false; navigate(selection == 2 ? interface_page::open_theatre : selection == 3 ? interface_page::theatre_delete : interface_page::theatre_records); selection = 0; }
        break;
      case 5: combat_invulnerable = !combat_invulnerable; campaign.combat_invincible(combat_invulnerable); break;
      case 6: controls.beginner_flight = !controls.beginner_flight; campaign.beginner_flight(controls.beginner_flight); break;
      case 7: comments_enabled = !comments_enabled; campaign.inflight_comments(comments_enabled); break;
      case 8: information(3); break;
      case 9: navigate(interface_page::menu); break;
      }
      break;
    case interface_page::new_theatre:
      {
        auto const &definition{world::theatres(group)[selection]};
        campaign.open_theatre(definition.name, world::load_theatre(definition.name));
        saved_theatres = campaign.theatres();
        navigate(interface_page::theatres);
      }
      break;
    case interface_page::open_theatre: campaign.activate_theatre(selection); navigate(interface_page::theatres); break;
    case interface_page::theatre_delete:
      if(saved_theatres[selection].objectives_remaining == 0) { unsigned int const chosen{selection}; navigate(interface_page::completed_theatre); selection = chosen; break; }
      [[fallthrough]];
    case interface_page::completed_theatre:
      campaign.delete_theatre(selection); saved_theatres = campaign.theatres(); navigate(interface_page::theatres); break;
    case interface_page::no_objectives: navigate(interface_page::starting_airfield); break;
    case interface_page::theatre_records:
      if(detailed_records) { detailed_records = false; navigate(interface_page::theatres); }
      else detailed_records = true;
      break;
    case interface_page::starting_airfield:
      if(prepared_world && world::multiple_airfields(*prepared_world)) {
        starting_airfield = selection;
        prepared_launch->airfield = starting_airfield;
        world::select_airfield(*prepared_world, starting_airfield);
      }
      if(prepared_world && resources::survey_airfield(*prepared_world, starting_airfield).planes == 0) { navigate(interface_page::no_aircraft); break; }
      [[fallthrough]];
    case interface_page::no_aircraft:
    case interface_page::mission_briefing:
      pending_launch = std::exchange(prepared_launch, std::nullopt);
      break;
    case interface_page::airfield_information: navigate(interface_page::starting_airfield); break;
    case interface_page::briefing:
      pending_relocation = tower.selected_start();
      page = interface_page::flight;
      break;
    case interface_page::high_score_menu:
      if(selection < 3) { score_list = selection; navigate(interface_page::high_scores); }
      else if(selection == 3) navigate(interface_page::score_delete);
      else if(selection == 4) information(7);
      else navigate(interface_page::menu);
      break;
    case interface_page::score_delete:
      if(selection < 3) { score_list = selection; navigate(interface_page::score_delete_confirmation); }
      else if(selection == 3) information(8);
      else navigate(interface_page::high_score_menu);
      break;
    case interface_page::score_delete_confirmation: campaign.clear_scores(score_list); navigate(interface_page::score_delete); break;
    case interface_page::high_scores: navigate(interface_page::high_score_menu); break;
    case interface_page::menu_information: navigate(information_return); break;
    case interface_page::documentation:
      if(document_chapter == 0 && !numeric_input.empty()) {
        unsigned int const chapter{static_cast<unsigned int>(std::stoi(numeric_input))};
        if(chapter < 1 || chapter > (campaign.current_campaign() == world::campaign_id::earth ? 17u : 22u)) break;
        document_chapter = chapter;
      } else if(++document_chapter > (campaign.current_campaign() == world::campaign_id::earth ? 17u : 22u)) document_chapter = 0;
      numeric_input.clear(); document(document_text("corncob.doc"), document_chapter); break;
    case interface_page::registration: navigate(information_return); break;
    case interface_page::notice: feedback.clear(); navigate(information_return); break;
    case interface_page::help: if(++help_index == help_screens.size()) navigate(return_page); break;
    case interface_page::quit_confirmation:
      if(selection == 0) pending_finish = true;
      else page = interface_page::flight;
      break;
    case interface_page::outcome:
      session_running = false;
      navigate(latest_result ? interface_page::sortie_summary : interface_page::menu);
      break;
    case interface_page::flight: break;
    }
  } catch(std::exception const &error) { notice(error.what()); }
}
void front_end::command(interface_command const command) {
  /// Text readers scroll; entry lists navigate; confirmation dialogs accept only their documented keys
  if(page == interface_page::briefing) {
    if(tower.lines().empty()) { page = interface_page::flight; return; }
    switch(command) {
    case interface_command::up: tower.move(-1); break;
    case interface_command::down: tower.move(1); break;
    case interface_command::previous: tower.move(-1); break;
    case interface_command::next: tower.move(1); break;
    case interface_command::page_up: tower.move(-tower_briefing::full_page); break;
    case interface_command::page_down: tower.move(tower_briefing::full_page); break;
    case interface_command::home: tower.home(); break;
    case interface_command::end: tower.end(); break;
    case interface_command::accept: accept(); break;
    case interface_command::cancel: case interface_command::backspace: page = interface_page::flight; break;
    default: break;
    }
    return;
  }
  if(page == interface_page::completed_theatre) { navigate(interface_page::theatres); return; }
  bool const confirmation{page == interface_page::pilot_delete || page == interface_page::pilot_resurrect || page == interface_page::score_delete_confirmation};
  if(confirmation && command != interface_command::accept) {
    navigate(page == interface_page::score_delete_confirmation ? interface_page::score_delete : interface_page::roster);
    return;
  }
  bool const reading{page == interface_page::menu_information || page == interface_page::documentation || page == interface_page::registration || page == interface_page::airfield_information};
  unsigned int const visible_lines{page == interface_page::airfield_information ? 15u : page == interface_page::menu_information ? 15u : 22u};
  auto scroll_to{[&](unsigned int const line){
    unsigned int count{static_cast<unsigned int>(document_lines.size())};
    scroll = std::min(line, count > visible_lines ? count - visible_lines : 0u);
  }};
  switch(command) {
  case interface_command::any_key: break;
  case interface_command::accept: accept(); break;
  case interface_command::backspace:
    if(editing_name()) {
      auto &text{page == interface_page::training_value ? numeric_input : entered_name};
      if(!text.empty()) text.pop_back();
    } else if(page == interface_page::documentation && document_chapter == 0 && !numeric_input.empty()) numeric_input.pop_back();
    else this->command(interface_command::cancel);
    break;
  case interface_command::up:
  case interface_command::down:
    if(reading) scroll_to(command == interface_command::up ? (scroll == 0 ? 0 : scroll - 1) : scroll + 1);
    else if(page == interface_page::records || page == interface_page::medals) {
      unsigned int const count{static_cast<unsigned int>(campaign.pilots().size())};
      pilot_index = (pilot_index + (command == interface_command::up ? count - 1 : 1)) % count;
    } else {
      unsigned int const count{choice_count()};
      selection = (selection + (command == interface_command::up ? count - 1 : 1)) % count;
    }
    break;
  case interface_command::home: if(reading) scroll = 0; else selection = 0; break;
  case interface_command::end: if(reading) scroll_to(static_cast<unsigned int>(document_lines.size())); else selection = choice_count() - 1; break;
  case interface_command::page_down: if(reading) scroll_to(scroll + visible_lines - 1); break;
  case interface_command::page_up: if(reading) scroll_to(scroll >= visible_lines - 1 ? scroll - visible_lines + 1 : 0); break;
  case interface_command::details:
    if(page == interface_page::records || page == interface_page::medals) detailed_records = !detailed_records;
    break;
  case interface_command::previous:
    if(page == interface_page::control_setup) adjust_setting(-1);
    if(page == interface_page::new_theatre) { choose_group(static_cast<world::theatre_group>((std::to_underlying(group) + 2) % 3)); selection = 0; }
    if(page == interface_page::help && help_index != 0) --help_index;
    break;
  case interface_command::next:
    if(page == interface_page::control_setup) adjust_setting(1);
    if(page == interface_page::new_theatre) { choose_group(static_cast<world::theatre_group>((std::to_underlying(group) + 1) % 3)); selection = 0; }
    if(page == interface_page::help || page == interface_page::story || page == interface_page::title) accept();
    break;
  case interface_command::cancel:
    switch(page) {
    case interface_page::control_setup: save_settings(); navigate(interface_page::menu); break;
    case interface_page::bindings: binding_capture = false; navigate(return_page); break;
    case interface_page::radio: accept(); break;
    case interface_page::ending: ending.reset(); show_outcome(14); break;
    case interface_page::demonstrations: navigate(interface_page::campaigns); break;
    case interface_page::campaigns: navigate(interface_page::menu); break;
    case interface_page::flight: ask_to_end_sortie(); break;
    case interface_page::help: navigate(return_page); break;
    case interface_page::menu: if(session_running) page = interface_page::flight; else quit_requested = true; break;
    case interface_page::mission_briefing: prepared_launch.reset(); navigate(interface_page::training); break;
    case interface_page::no_objectives:
    case interface_page::no_aircraft:
    case interface_page::starting_airfield: prepared_launch.reset(); navigate(interface_page::theatres); break;
    case interface_page::airfield_information: navigate(interface_page::starting_airfield); break;
    case interface_page::pilots: case interface_page::pilot_name: case interface_page::pilot_delete:
    case interface_page::records: case interface_page::medals: case interface_page::pilot_resurrect: navigate(interface_page::roster); break;
    case interface_page::pilot_activate: selection = 1; accept(); break;
    case interface_page::completed_theatre:
    case interface_page::new_theatre: case interface_page::open_theatre: case interface_page::theatre_delete:
    case interface_page::theatre_records: detailed_records = false; navigate(interface_page::theatres); break;
    case interface_page::training_counts: case interface_page::training_wickedness: navigate(interface_page::training); break;
    case interface_page::training_value: feedback.clear(); navigate(training_editor); break;
    case interface_page::score_delete: case interface_page::high_scores: navigate(interface_page::high_score_menu); break;
    case interface_page::score_delete_confirmation: navigate(interface_page::score_delete); break;
    case interface_page::menu_information: navigate(information_return); break;
    case interface_page::documentation:
      if(document_chapter != 0) { document_chapter = 0; document(document_text("corncob.doc"), 0); }
      else navigate(interface_page::menu);
      break;
    case interface_page::unavailable_pilot: case interface_page::resurrect_activate: navigate(interface_page::roster); break;
    case interface_page::campaign_victory: case interface_page::post_mission_report: case interface_page::career_announcement: case interface_page::sortie_summary: accept(); break;
    case interface_page::notice: accept(); break;
    case interface_page::title: case interface_page::story: title.reset(); navigate(interface_page::menu); break;
    case interface_page::boss: page = boss_return; break;
    case interface_page::current_score: case interface_page::quit_confirmation: page = interface_page::flight; break;
    default: navigate(interface_page::menu); break;
    }
    break;
  case interface_command::help:
    if(page != interface_page::bindings) { return_page = page; navigate(interface_page::bindings); }
    break;
  }
}
void front_end::begin_ending(graphics::indexed_framebuffer const &backdrop, std::array<render::rgba_pixel, 16> const &colours) {
  /// Preserve the final sortie page while the single executable performs the original SRATS handoff
  ending.emplace(backdrop, colours);
  page = interface_page::ending;
}
void front_end::advance(double const seconds) {
  /// Helper animations continue on wall time while flight state remains stopped
  if(page == interface_page::story) {
    double constexpr palette_steps_per_second{40.0}, maximum_intensity{16.0};
    story_intensity = std::clamp(story_intensity + seconds * palette_steps_per_second * (advancing_story ? -1.0 : 1.0), 0.0, maximum_intensity);
    if(advancing_story && story_intensity == 0.0) {
      advancing_story = false;
      if(++story_index == stories.size()) { title.emplace(assets); page = interface_page::title; }
    }
  }
  if(page == interface_page::title) title->advance(seconds);
  if(page == interface_page::ending) {
    ending->advance(seconds);
    if(ending->finished()) { ending.reset(); show_outcome(14); }
  }
}
void front_end::show_radio(resources::radio_notice const notice, render::framebuffer const &backdrop, bool const enter_map) {
  /// Preserve the original warning layout while showing the current bindings.
  radio = resources::read_radio_message(notice);
  auto label{[&](control_id const id){
    auto const found{std::ranges::find(settings.bindings, id, &control_binding::id)};
    return found == settings.bindings.end() ? std::string{"Unbound"} : binding_name(*found);
  }};
  if(notice == resources::radio_notice::ground_steering) {
    radio->lines[1].text = "  Rudder: " + label(control_id::rudder_left) + " / " + label(control_id::rudder_right);
    radio->lines[2].text = "  Brakes: " + label(control_id::brakes);
    radio->lines[3].text = "  Hold the brake control until the aircraft stops.";
  } else if(notice == resources::radio_notice::low_altitude) {
    radio->lines[4].text = "  Pull up: " + label(control_id::pitch_up);
  } else if(notice == resources::radio_notice::leave_on_foot) {
    radio->lines[1].text = "  Call rescue: " + label(control_id::rescue);
  } else if(notice == resources::radio_notice::training) {
    radio->lines[1].text = "    F4: current control bindings";
  }

  radio_backdrop = backdrop;
  map_after_radio = enter_map;
  page = interface_page::radio;
}
void front_end::leave_sortie() {
  /// The flight session has already applied the original one-time Escape warning rule
  pending_finish = true;
}
void front_end::abort_sortie() {
  /// Control-C is a distinct original exit path with the abort result bit and no awarded score
  pending_abort = true;
  pending_finish = true;
}
bool front_end::take_abort() {
  /// Consume abort status alongside the single finalization request
  return std::exchange(pending_abort, false);
}
bool front_end::take_map() {
  /// Map instructions return to the original retained session before entering its independent observer loop
  return std::exchange(pending_map, false);
}
void front_end::show_menu() {
  /// The native helper flow can return to a retained flight without recreating its world
  if(page == interface_page::flight) session_running = true;
  page = interface_page::menu;
  selection = 0;
}
void front_end::finish_editing() {
  session_running = false;
  sync_pilot();
  navigate(interface_page::theatres);
}
void front_end::begin_flight() {
  /// A newly created native session owns the selected theatre until its sortie ends
  session_running = true;
  page = interface_page::flight;
}
void front_end::show_flight_help() {
  if(page == interface_page::bindings) return;
  return_page = page;
  session_running = true;
  navigate(interface_page::bindings);
}
void front_end::ask_to_end_sortie() {
  /// Escape requires a deliberate confirmation and keeps the current flight available on cancellation
  session_running = true;
  page = interface_page::quit_confirmation;
  selection = 1;
}
void front_end::show_outcome(unsigned int const index) {
  /// Outcome selection comes from sortie state; rendering retains the original stored composition
  outcome_index = std::min(index, static_cast<unsigned int>(outcomes.size() - 1));
  page = interface_page::outcome;
}
void front_end::draw_menu(graphics::interface_canvas &canvas, std::string_view const title, std::span<std::string_view const> const choices) const {
  /// Retain a simple BIOS-font interface, with selection and instructions visible at the same time
  canvas.clear();
  canvas.rectangle(4, 20, 636, 392, blue);
  canvas.box(0, 16, 640, 396, yellow);
  canvas.box(2, 18, 638, 394, white);
  canvas.rectangle(168, 0, 464, 76, blue);
  canvas.box(172, 4, 460, 72, white);
  canvas.text(272, 12, "Corncob 3D", white);
  canvas.text(316 - static_cast<int>(title.size()) * 4, 44, title, grey);
  if(!choices.empty()) canvas.text(80, 92, page == interface_page::training || page == interface_page::control_setup
    ? "Left/Right changes values; Enter selects" : "Use arrow keys and Enter to choose an action:", {85, 255, 255});
  unsigned int const first{selection < 8 ? 0 : selection - 7};
  for(unsigned int n{first}; n < choices.size() && n < first + 8; ++n) {
    int const y{112 + static_cast<int>(n - first) * 24};
    if(n == selection) canvas.rectangle(96, y, 600, y + 16, {0, 0, 85});
    canvas.text(112, y, choices[n], n == selection ? yellow : grey);
  }
  if(!feedback.empty()) canvas.text(24, 316, feedback.substr(0, 74), yellow);
  canvas.text(72, 348, "Arrows select   Enter accepts   Esc returns", grey);
  canvas.text(72, 368, "F4: flight controls and help", grey);
}
void front_end::render(render::framebuffer &target) const {
  /// Menus and helper-program artwork share the native window but retain their original video-mode pixel sizes
  if(page == interface_page::radio) {
    target = radio_backdrop;
    auto draw_message{[&](render::framebuffer &image){
      graphics::interface_canvas canvas{image, assets.file("font14.bin"), 14};
      auto fill{[&](resources::message_box const &box, render::rgba_pixel const colour){canvas.rectangle(box.left, box.top, box.left + box.width, box.top + box.height, colour);}};
      fill(radio->border, {170, 0, 0});
      fill(radio->background, {0, 0, 0});
      for(auto const &line : radio->lines) {
        auto const columns{static_cast<unsigned int>(std::max(0, (radio->background.left + radio->background.width - line.column * 8) / 8))};
        canvas.text(line.column * 8, line.row * 14, std::string_view{line.text}.substr(0, columns), grey);
      }
    }};
    if(target.width == render::screen_layout::width && target.height == render::screen_layout::height) {
      draw_message(target);
    } else {
      // Adapt the message alone; changing the backdrop's dimensions would change its displayed integer scale.
      render::framebuffer message;
      draw_message(message);
      auto const &box{radio->border};
      for(unsigned int y{0}; y != target.height; ++y) for(unsigned int x{0}; x != target.width; ++x) {
        int const source_x{static_cast<int>(x * message.width / target.width)};
        int const source_y{static_cast<int>(y * message.height / target.height)};
        if(source_x < box.left || source_x >= box.left + box.width || source_y < box.top || source_y >= box.top + box.height) continue;
        target.pixels[y * target.width + x] = message.pixels[static_cast<unsigned int>(source_y) * message.width + static_cast<unsigned int>(source_x)];
      }
    }
    return;
  }
  if(page == interface_page::ending) { ending->render(target); return; }
  if(page == interface_page::briefing) { tower.render(target, assets.file("font16.bin")); return; }
  if(page == interface_page::boss) {
    graphics::interface_canvas canvas{target, assets.file("font16.bin"), 16, 640, 400};
    canvas.clear();
    auto text{std::string{assets.text("boss.txt")}};
    if(auto const end{text.find('$')}; end != std::string::npos) text.resize(end);
    canvas.text(0, 0, text, grey);
    return;
  }
  if(page == interface_page::story) {
    graphics::draw_presentation(target, assets, stories.at(story_index));
    unsigned int const intensity{static_cast<unsigned int>(story_intensity)};
    if(intensity < 16) {
      auto channel{[intensity](uint8_t const value){
        unsigned int const dac{(value >> 2) * intensity / 16};
        return static_cast<uint8_t>((dac << 2) | (dac >> 4));
      }};
      for(auto &pixel : target.pixels) pixel = {
        .red{channel(pixel.red)},
        .green{channel(pixel.green)},
        .blue{channel(pixel.blue)},
      };
    }
    return;
  }
  if(page == interface_page::outcome) {
    graphics::draw_presentation(target, campaign_assets(), outcomes.at(outcome_index));
    return;
  }
  if(page == interface_page::help) {
    resources::draw_cockpit_preview(help_screens.at(help_index), target);
    target.width = 640;
    target.height = 350;
    return;
  }
  if(page == interface_page::title) {
    title->render(target);
    return;
  }
  render_menus(target);
}
unsigned int front_end::choice_count() const {
  /// Counts describe the original selectable entries, excluding status text and panel decorations
  switch(page) {
  case interface_page::menu: return session_running ? 4 : 11;
  case interface_page::roster: return 9;
  case interface_page::theatres: return 10;
  case interface_page::training: return 7;
  case interface_page::control_setup: return 15;
  case interface_page::bindings: return static_cast<unsigned int>(settings.bindings.size()) + 2;
  case interface_page::training_counts: case interface_page::training_wickedness: return 4;
  case interface_page::campaigns: return 9;
  case interface_page::demonstrations: return 4;
  case interface_page::starting_airfield: return prepared_world && world::multiple_airfields(*prepared_world) ? 9 : 1;
  case interface_page::new_theatre: return static_cast<unsigned int>(world::theatres(group).size());
  case interface_page::open_theatre: case interface_page::theatre_delete: case interface_page::theatre_records:
    return std::max(1u, static_cast<unsigned int>(saved_theatres.size()));
  case interface_page::high_score_menu: return 6;
  case interface_page::score_delete: return 5;
  case interface_page::pilot_activate: case interface_page::quit_confirmation: return 2;
  case interface_page::pilots: return std::max(1u, static_cast<unsigned int>(campaign.pilots().size()));
  default: return 1;
  }
}
audio::music_cue front_end::music() const noexcept {
  /// The DOS launcher plays music; flight, its helper pages and the original outcome/ending sequences do not
  if(!music_on || session_running || page == interface_page::ending || page == interface_page::outcome || page == interface_page::story)
    return audio::music_cue::silent;
  if(campaign.current_campaign() == world::campaign_id::other_worlds) return page == interface_page::title ? audio::music_cue::other_title : audio::music_cue::other_menus;
  return page == interface_page::title ? audio::music_cue::title : audio::music_cue::menus;
}
preferences &front_end::options() noexcept {
  return settings;
}
void front_end::set_fullscreen(bool const enabled) {
  settings.fullscreen = enabled;
  try { save_settings(); } catch(std::exception const &error) { notice(error.what()); }
}
void front_end::save_settings() {
  settings.save(campaign.save_directory() / "redux.cfg");
}
bool front_end::capturing_binding() const noexcept {
  return binding_capture;
}
bool front_end::bind_input(int const key, int const mouse) {
  if(!binding_capture || (key < 0 && mouse < 0)) return false;
  binding_capture = false;
  if(key >= 290 && key <= 293) { notice("F1-F4 are reserved for boss, intelligence, score and control help."); return true; }
  if(key == 256) return true; // Escape cancels capture.
  auto &binding{settings.bindings.at(selection)};
  bool const clear{key == 259 || key == 261};
  if(!clear) for(auto &other : settings.bindings) {
    if(other.id == binding.id || (other.context != binding.context && other.context != control_context::both && binding.context != control_context::both)) continue;
    if(key >= 0) { if(other.key == key) other.key = -1; if(other.alternate == key) other.alternate = -1; }
    if(mouse >= 0 && other.mouse == mouse) other.mouse = -1;
  }
  if(clear) { binding.key = binding.alternate = binding.mouse = -1; }
  else if(mouse >= 0) binding.mouse = mouse;
  else { binding.key = key; binding.alternate = -1; }
  try { save_settings(); } catch(std::exception const &error) { notice(error.what()); }
  return true;
}
void front_end::adjust_setting(int const direction) {
  switch(selection) {
  case 1: controls.joystick_device = controls.joystick_device == -2 ? -1 : -2; campaign.keyboard_control(controls.joystick_device == -2); break;
  case 2: controls.beginner_flight = !controls.beginner_flight; campaign.beginner_flight(controls.beginner_flight); break;
  case 3: settings.scene.horizontal_fov = std::clamp(settings.scene.horizontal_fov + direction * 2, graphics::scene_settings::minimum_fov, graphics::scene_settings::maximum_fov); break;
  case 4: settings.scene.draw_distance = std::max(0.0, settings.scene.draw_distance + direction * 5); break;
  case 5: settings.scene.ground_points = static_cast<unsigned int>(std::clamp(static_cast<int>(settings.scene.ground_points) + direction * static_cast<int>(graphics::scene_settings::original_ground_points), 0, static_cast<int>(graphics::scene_settings::maximum_ground_points))); break;
  case 6:
    {
      std::array<render::video_size, 6> constexpr sizes{{{640,350}, {960,540}, {1280,720}, {1600,900}, {1920,1080}, {2560,1440}}};
      int index{-1};
      if(!settings.window_resolution) for(unsigned int n{0}; n < sizes.size(); ++n) if(sizes[n].width == settings.scene.resolution.width && sizes[n].height == settings.scene.resolution.height) index = static_cast<int>(n);
      index += direction;
      if(index > 5) index = -1;
      if(index < -1) index = 5;
      settings.window_resolution = index == -1;
      if(index >= 0) settings.scene.resolution = sizes[static_cast<unsigned int>(index)];
    }
    break;
  case 7: settings.mouse_sensitivity = std::clamp(settings.mouse_sensitivity + direction * 2, 1.0, 200.0); break;
  case 8: settings.invert_mouse = !settings.invert_mouse; break;
  case 9: settings.scene.ground_range = std::clamp(settings.scene.ground_range * (direction > 0 ? 2.0 : 0.5), graphics::scene_settings::minimum_ground_range, graphics::scene_settings::maximum_ground_range); break;
  case 10: settings.scene.gunsight_size = std::clamp(settings.scene.gunsight_size + direction * 0.25, graphics::scene_settings::minimum_gunsight_size, graphics::scene_settings::maximum_gunsight_size); break;
  case 11: settings.music_volume = std::clamp(settings.music_volume + direction * 5, 0.0, 100.0); break;
  case 12: settings.effects_volume = std::clamp(settings.effects_volume + direction * 5, 0.0, 100.0); break;
  case 13: settings.fullscreen = !settings.fullscreen; break;
  default: return;
  }
  save_settings();
}
void front_end::wheel(int const steps) {
  // Wheel motion stops at list ends; keyboard navigation retains wraparound.
  if(binding_capture || steps == 0) return;
  if(page == interface_page::briefing) { tower.move(steps * 3); return; }
  if(page == interface_page::documentation || page == interface_page::registration || page == interface_page::menu_information || page == interface_page::airfield_information) {
    for(int n{0}; n < std::abs(steps) * 3; ++n) command(steps < 0 ? interface_command::up : interface_command::down);
  } else if(page != interface_page::flight && page != interface_page::radio && page != interface_page::story && page != interface_page::title) {
    selection = static_cast<unsigned int>(std::clamp(static_cast<int>(selection) + steps, 0, static_cast<int>(choice_count()) - 1));
  }
}
control_setup const &front_end::flight_controls() const noexcept {
  /// New sessions and platform polling consume the same settings shown by the front end
  return controls;
}
void front_end::show_intelligence(world::theatre const &world, unsigned int const airfield) {
  /// F2 and walking into a tower run the same original helper, retaining its form-feed sections and visible mission selection
  tower = tower_briefing{world.briefing, airfield};
  pending_relocation.reset();
  session_running = true;
  page = interface_page::briefing;
}
void front_end::show_boss() {
  /// F1 retains the original deliberately uninteresting DOS prompt disguise
  if(page == interface_page::briefing && tower.lines().empty()) { page = interface_page::flight; return; }
  session_running = true;
  boss_return = page == interface_page::briefing ? interface_page::briefing : interface_page::flight;
  page = interface_page::boss;
}
void front_end::show_current_score(sortie_statistics const &result, bool const training) {
  /// F3 inspects this sortie without finalizing or saving it
  training_report = training;
  latest_result = result;
  session_running = true;
  page = interface_page::current_score;
}
std::optional<world::mission_start> front_end::take_relocation() {
  /// Consume a mission selection without recreating the theatre or losing already destroyed objectives
  return std::exchange(pending_relocation, std::nullopt);
}
void front_end::character(uint32_t const codepoint) {
  if(page == interface_page::control_setup || page == interface_page::bindings) return;
  if(page == interface_page::menu && !session_running && (codepoint == 'r' || codepoint == 'R')) {
    document(document_text("moag.inf"), 9);
    if(!document_lines.empty()) document_lines.erase(document_lines.begin());
    information_return = interface_page::menu;
    navigate(interface_page::registration);
    return;
  }
  if(page == interface_page::new_theatre && (codepoint == 'b' || codepoint == 'B')) {
    auto const &definition{world::theatres(group)[selection]};
    campaign.open_theatre(definition.name, world::load_theatre(definition.name));
    saved_theatres = campaign.theatres();
    pending_launch = launch_request{
      .theatre{std::string{definition.name}},
      .invincible{true},
      .comments{false},
      .editing{true},
    };
    return;
  }
  /// Text prompts own characters; elsewhere the yellow original shortcut executes its menu entry immediately
  if(page == interface_page::briefing) {
    if(tower.lines().empty()) { page = interface_page::flight; return; }
    switch(codepoint) {
    case 6: case 14: case 'j': case '2': case '6': command(interface_command::down); break;
    case 2: case 16: case 'k': case '4': case '8': command(interface_command::up); break;
    case 25: case ' ': command(interface_command::page_down); break;
    case 'd': tower.move(tower_briefing::half_page); break;
    case 'u': tower.move(-tower_briefing::half_page); break;
    case 20: case 'g': command(interface_command::home); break;
    case 18: case 'G': command(interface_command::end); break;
    case 15: command(interface_command::page_up); break;
    case 8: case 27: case 'q': command(interface_command::cancel); break;
    case 10: case 13: command(interface_command::accept); break;
    default: break;
    }
    return;
  }
  if(page == interface_page::pilot_name) {
    if(codepoint >= 32 && codepoint < 127 && entered_name.size() < 20) entered_name += static_cast<char>(codepoint);
    return;
  }
  if(codepoint == 32 && (page == interface_page::high_scores || page == interface_page::notice || page == interface_page::help || page == interface_page::story || page == interface_page::title)) return;
  if(page == interface_page::training_value || (page == interface_page::documentation && document_chapter == 0)) {
    if(codepoint >= '0' && codepoint <= '9' && numeric_input.size() < 2) numeric_input += static_cast<char>(codepoint);
    return;
  }
  if(page == interface_page::unavailable_pilot || page == interface_page::resurrect_activate) {
    bool const resurrecting{page == interface_page::unavailable_pilot};
    if((codepoint == 'y' || codepoint == 'Y') && (!resurrecting || campaign.pilots()[pilot_index].unavailable())) {
      if(resurrecting) campaign.resurrect(pilot_index);
      campaign.activate(pilot_index);
      sync_pilot();
    }
    navigate(interface_page::roster);
    return;
  }
  if(page == interface_page::completed_theatre) {
    if(codepoint == 'y' || codepoint == 'Y') accept();
    else navigate(interface_page::theatres);
    return;
  }
  if(page == interface_page::campaign_victory || page == interface_page::post_mission_report || page == interface_page::no_objectives || page == interface_page::career_announcement) { accept(); return; }
  if(page == interface_page::no_aircraft) {
    auto const active{campaign.active_theatre()};
    if((codepoint == 'p' || codepoint == 'P') && active && saved_theatres[*active].stored_aircraft != 0) {
      prepared_launch->reserve_aircraft = true;
      accept();
    } else navigate(interface_page::starting_airfield);
    return;
  }
  if(page == interface_page::starting_airfield && (codepoint == 'i' || codepoint == 'I')) {
    document_lines = prepared_world ? resources::airfield_survey(*prepared_world, world::multiple_airfields(*prepared_world) ? selection : starting_airfield) : std::vector<std::string>{};
    scroll = 0;
    navigate(interface_page::airfield_information);
    return;
  }
  if(page == interface_page::pilot_delete || page == interface_page::pilot_resurrect || page == interface_page::score_delete_confirmation) { command(interface_command::cancel); return; }
  if(page == interface_page::pilot_activate) {
    if(codepoint == 'y' || codepoint == 'Y' || codepoint == 'n' || codepoint == 'N') { selection = codepoint == 'n' || codepoint == 'N' ? 1 : 0; accept(); }
    return;
  }
  if(page == interface_page::high_scores || page == interface_page::notice || page == interface_page::current_score || page == interface_page::sortie_summary) { accept(); return; }
  std::string_view shortcuts;
  switch(page) {
  case interface_page::menu: if(!session_running) shortcuts = "dcthsomvpix"; break;
  case interface_page::campaigns: shortcuts = "eopdbihtr"; break;
  case interface_page::demonstrations: shortcuts = "123r"; break;
  case interface_page::roster: shortcuts = "cadlenvir"; break;
  case interface_page::theatres: shortcuts = "foadvpmcir"; break;
  case interface_page::training: shortcuts = "fwndpir"; break;
  case interface_page::training_counts: case interface_page::training_wickedness: shortcuts = "mgar"; break;
  case interface_page::control_setup: case interface_page::bindings: break;
  case interface_page::high_score_menu: shortcuts = "scfdir"; break;
  case interface_page::score_delete: shortcuts = "scfir"; break;
  case interface_page::story: case interface_page::title: accept(); return;
  default: break;
  }
  if(codepoint >= 128) return;
  auto const index{shortcuts.find(static_cast<char>(std::tolower(static_cast<unsigned char>(codepoint))))};
  if(index != std::string_view::npos) { selection = static_cast<unsigned int>(index); accept(); }
}
bool front_end::editing_name() const noexcept {
  /// Space and Backspace belong to the active text prompt, rather than triggering menu navigation
  return page == interface_page::pilot_name || page == interface_page::training_value;
}
bool front_end::take_finish() {
  /// Consume one confirmed end request; the application records and presents the actual result
  return std::exchange(pending_finish, false);
}
void front_end::finish_sortie(sortie_statistics const &result, pilot_record const *const previous, pilot_record const *const previous_theatre) {
  /// Match native result priority before showing the sortie statistics in the same executable
  latest_result = result;
  auto const *current{campaign.active_pilot()};
  pending_campaign_victory = campaign.current_campaign() == world::campaign_id::other_worlds && previous && current
    && previous->rank() != pilot_rank::general && current->rank() == pilot_rank::general && !result.has(sortie_status::killed);
  mission_prior = previous_theatre ? *previous_theatre : previous ? *previous : pilot_record{};
  returned_pilot = campaign.active_pilot() ? std::optional{*campaign.active_pilot()} : std::nullopt;
  mission_report_title = "MISSION REPORT";
  if(previous_theatre) {
    auto const active{campaign.active_theatre()};
    auto const worlds{campaign.theatres()};
    if(active && *active < worlds.size()) mission_report_title += ":  " + worlds[*active].title + " Theater of Operations";
  }
  announcements.clear();
  if(previous) if(auto const *pilot{campaign.active_pilot()}) {
    bool const posthumous{result.has(sortie_status::killed)};
    std::string const salutation{pilot->resurrections() != 0 ? std::format(" ({} res.),", pilot->resurrections()) : posthumous ? "," : "!"};
    auto const greeting{std::string{posthumous ? "" : "Congratulations "} + std::string{rank_abbreviation(pilot->rank())} + " " + pilot->name() + salutation};
    unsigned int const promotions{static_cast<unsigned int>(std::to_underlying(pilot->rank()) - std::to_underlying(previous->rank()))};
    std::array<std::string_view, 4> constexpr praise{"", "dazzling ", "fantastic ", "really-beyond-belief "};
    if(promotions != 0) announcements.push_back({
      .lines{(posthumous ? "Former " : "Congratulations former ") + std::string{rank_abbreviation(previous->rank())} + " " + pilot->name() + salutation,
        std::string{posthumous ? "For a " : "For your "} + std::string{praise[std::min(promotions - 1, 3u)]} + "display of combat",
        posthumous ? "prowess, is posthumously promoted to the rank of" : "prowess, you have been promoted to the rank of"},
      .promotion{pilot->rank()},
    });
    std::array<medal, 10> constexpr presentation_order{medal::service_award, medal::purple_heart, medal::bronze_star, medal::silver_star, medal::gold_star,
      medal::stellar_cluster, medal::distinguished_service_cross, medal::theatre_combat, medal::distinguished_flying_cross, medal::combat_master};
    std::array<std::string_view, 10> constexpr citations{
      "surviving extreme injury in combat", "inflicting heavy damage to the enemy", "inflicting severe damage to the enemy",
      "inflicting extreme damage to the enemy", "extraordinary accomplishment of objectives", "surviving numerous combat sorties",
      "inflicting catastrophic damage to the enemy", "Completely obliterating the Alien threat",
      "extraordinary services without losing a plane", "completing all mission objectives within a T-of-O",
    };
    for(auto const kind : presentation_order) if(pilot->awards(kind) != previous->awards(kind)) announcements.push_back({
      .lines{greeting, "For " + std::string{citations[std::to_underlying(kind) - std::to_underlying(medal::purple_heart)]} + (posthumous ? "," : ""),
        std::string{posthumous ? "is posthumously " : "you have been "} + (kind == medal::combat_master ? "designated a" : "awarded the")},
      .decoration{kind},
    });
  }
  session_running = false;
  unsigned int panel{9};
  if(result.has(sortie_status::earth_saved)) panel = 14;
  else if(result.has(sortie_status::killed)) {
    panel = result.has(sortie_status::ejected) ? 7 : 4;
    if(result.has(sortie_status::ejected)) switch(static_cast<world::object_type>(result.contacting_type)) {
    case world::object_type::aa_shell: case world::object_type::shell: panel = 10; break;
    case world::object_type::kla: panel = 11; break;
    case world::object_type::crater: panel = 12; break;
    case world::object_type::bee_swarm: case world::object_type::barrage_balloon: panel = 13; break;
    default: break;
    }
  } else if(result.has(sortie_status::captured)) panel = 8;
  else if(result.has(sortie_status::home) && result.friendly_destroyed != 0) panel = 6;
  else if(result.has(sortie_status::rescued)) panel = 1;
  else if(result.has(sortie_status::crashed_away)) panel = 2;
  else if(result.has(sortie_status::crashed_home)) panel = 3;
  else if(result.has(sortie_status::home)) panel = result.has(sortie_status::landed) ? 0 : 5;
  show_outcome(panel);
}
std::optional<launch_request> front_end::take_launch() {
  /// Consume launch requests once so the main loop can replace its owned session safely
  return std::exchange(pending_launch, std::nullopt);
}
bool front_end::in_flight() const noexcept {
  /// Simulation and flight controls run only while the flight page owns input
  return page == interface_page::flight;
}
bool front_end::quitting() const noexcept {
  /// Keep window-system shutdown outside the core menu state machine
  return quit_requested;
}
interface_page front_end::current_page() const noexcept {
  /// Expose page state for scripted UI traversal without coupling to GLFW
  return page;
}

} // namespace corncob::game

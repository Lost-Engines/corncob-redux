#include <algorithm>
#include <format>
#include <numeric>
#include "front_end.h"
#include "game/report_format.h"
#include "graphics/menu_canvas.h"
#include "maths/integer.h"
#include "resources/airfield_survey.h"
#include "resources/assets.h"
#include "world/airfields.h"
#include "world/theatre_catalog.h"

namespace corncob::game {

namespace {

std::array<std::string_view, 11> constexpr main_choices{
  "\\Duty roster", "\\Combat missions", "\\Training missions", "display \\High score list", "\\Select campaign / world group",
  "\\Options and controls", "\\Music toggle", "\\View  the Corncob 3D documentation",
  "save documentation as \\PDF", "get \\Information about this menu", "e\\Xit game",
};
std::array<std::string_view, 9> constexpr roster_choices{
  "\\Create Pilot", "\\Activate Pilot", "\\Deactivate Pilot", "de\\Lete Pilot", "r\\Esurrect Pilot",
  "re\\Name Pilot", "\\View Pilot Records", "get \\Information about this menu", "\\Return to previous menu",
};
std::array<std::string_view, 10> constexpr combat_choices{
  "\\Fly mission", "\\Open a new theater of operations", "\\Activate an open theater", "\\Delete a theater",
  "\\View status of a theater", "toggle invulnerable \\Plane", "toggle aerodynamic \\Model", "toggle inflight \\Comments",
  "get \\Information about this menu", "\\Return to previous menu",
};
std::array<std::string_view, 7> constexpr training_choices{
  "\\Fly mission", "modify \\Wickedness of enemies", "choose \\Number of enemies", "choose \\Default values",
  "toggle invulnerable \\Plane", "get \\Information about this menu", "\\Return to previous menu",
};
std::array<std::string_view, 6> constexpr score_choices{
  "see list of high \\Scores", "see list of top \\Careers", "see list of \\Fastest careers",
  "\\Delete one of the above lists", "get \\Information about this menu", "\\Return to previous menu",
};
std::array<std::string_view, 5> constexpr delete_choices{
  "delete the list of top \\Scores", "delete the list of top \\Careers", "delete the list of \\Fastest careers",
  "get \\Information about this menu", "\\Return to previous menu",
};

int total(quality_counts const &counts) {
  /// Reports display the original signed, wrapping word totals rather than host-sized counters
  return maths::word(std::accumulate(counts.begin(), counts.end(), 0));
}
std::string pilot_label(pilot_record const *const pilot, bool const abbreviated = false) {
  /// Main menus spell ranks out; the narrower roster panels use MOAG's abbreviated rank names
  return pilot ? std::string{abbreviated ? rank_abbreviation(pilot->rank()) : rank_name(pilot->rank())} + " " + pilot->name()
    : std::string{abbreviated ? "2nd Lt. trainee" : "2nd Lieutenant trainee"};
}
void report(graphics::menu_canvas &screen, pilot_record const *const pilot, bool const detailed, std::string_view const title,
            std::optional<sortie_statistics> const sortie = std::nullopt, pilot_record const *const previous = nullptr) {
  /// The original overview includes decorations; Insert reveals the four statistical columns on the same report
  bool const preliminary{title.starts_with("PRELIMINARY REPORT") || title.starts_with("MISSION REPORT")};
  bool const post{title == "POST-MISSION CAREER REPORT"};
  score_context const calculation{title.starts_with("PRELIMINARY REPORT") ? score_context::flight_report : score_context::campaign};
  screen.colours(preliminary ? 15 : 7, preliminary ? 1 : 8, preliminary ? 14 : 9, 15);
  screen.panel(0, 0, 79, 24, preliminary ? 14 : 9, true, false);
  screen.text((78 - static_cast<int>(title.size())) / 2, 0, " " + std::string{title} + " ", preliminary ? 14 : 2);
  screen.text(16, 2, "PILOT:", preliminary ? 14 : 2); screen.text(24, 2, pilot_label(pilot), 15);
  auto const career{post && pilot ? pilot->last_sortie() : pilot ? pilot->career() : sortie.value_or(sortie_statistics{})};
  unsigned int const tenths{career.elapsed_seconds / 360};
  bool const columns{detailed || sortie.has_value()};
  bool const global{columns && title == "CAREER REPORT"};
  int const extra_rows{!columns ? 0 : global ? 2 : 1};
  std::string const hours{std::format("{}.{} hours flying time.", tenths < 10 ? std::string{} : std::to_string(tenths / 10), tenths % 10)};
  if(preliminary) {
    auto const &result{*sortie};
    auto const factors{calculate_score_factors(result, calculation)};
    screen.text(24, 3, std::format("Flying time = {}:{:02}:{:02}", result.elapsed_seconds / 3600, result.elapsed_seconds / 60 % 60, result.elapsed_seconds % 60), 14);
    screen.text(15, 4, "STATUS:", 14); screen.text(24, 4, sortie_description(result));
    screen.text(16, 5, "SCORE:", 14); screen.text(24, 5, std::to_string(calculate_score(result, calculation)));
    screen.text(35, 5, "BEST:", 14); screen.text(42, 5, std::to_string(pilot->best_sortie_score()));
    screen.text(53, 5, "TOTAL:", 14); screen.text(61, 5, std::to_string(pilot->career_score()));
    if(calculate_score(result, calculation) > (previous ? previous->best_sortie_score() : 0)) screen.text(25 + static_cast<int>(std::to_string(calculate_score(result, calculation)).size()), 5, "\x03", 12);
    screen.text(17, 6, "PMSF:", 14); screen.text(24, 6, std::to_string(factors.present));
    screen.text(31, 6, "MSF:", 14); screen.text(37, 6, std::to_string(factors.mission));
    screen.text(44, 6, "MOBF:", 14); screen.text(51, 6, std::to_string(factors.base));
  } else {
    screen.text(24, 3, std::format("{} mission{}{}", pilot ? pilot->sorties() : 0, pilot && pilot->sorties() == 1 ? "" : "s", columns ? std::string{} : ",  " + hours), 2);
    if(columns) screen.text(24, 3 + extra_rows, hours, 2);
    screen.text(15, 4 + extra_rows, "STATUS:", 2);
    screen.text(24, 4 + extra_rows, duty_description(pilot) + (pilot && pilot->condition(report_time()) == pilot_condition::stockade ? " until " + release_date(pilot->suspended_until()) + "." : ""), 15);
    screen.text(11, 5 + extra_rows, "HIGH SCORE:", 2); screen.text(24, 5 + extra_rows, std::to_string(pilot ? pilot->best_sortie_score() : 0), 15);
    screen.text(37, 5 + extra_rows, "TOTAL SCORE:", 2); screen.text(51, 5 + extra_rows, std::to_string(pilot ? pilot->career_score() : 0), 15);
  }
  int const first{9 + extra_rows}, secondary{15 + (global ? 1 : 0)};
  if(columns) {
    screen.text(33, 7 + extra_rows, preliminary ? "This      Career      Best       Career" : "Last      Career      Best       Career", preliminary ? 12 : 6);
    screen.text(32, 8 + extra_rows, "Mission     High      Mission      Total", preliminary ? 12 : 6);
  } else {
    screen.text(post ? 33 : 31, 7, post ? "This" : "Career", 6); screen.text(post ? 30 : 32, 8, post ? "Mission" : "Total", 6);
  }
  screen.text(8, 8 + extra_rows, "PRIMARY KILLS:", preliminary ? 14 : 2);
  screen.text(7, secondary - 1, "SECONDARY KILLS:", preliminary ? 14 : 2);
  struct report_row {
    std::string_view label;
    int row;
    int (*value)(sortie_statistics const &);
  };
  std::array<report_row, 9> const fields{
    report_row{
      .label{"Mission Objective:"},
      .row{first},
      .value{[](auto const &s)->int{return maths::word(s.objectives_completed);}},
    },
    report_row{
      .label{"TRFRU/Fuel Dumps:"},
      .row{first + 1},
      .value{[](auto const &s)->int{return maths::word(s.resource_kills);}},
    },
    report_row{
      .label{"AAA Batteries:"},
      .row{first + 2},
      .value{[](auto const &s)->int{return total(s.aaa_kills);}},
    },
    report_row{
      .label{"Other Sites:"},
      .row{first + 3},
      .value{[](auto const &s)->int{return maths::word(total(s.site_kills) + total(s.orb_kills));}},
    },
    report_row{
      .label{"Ground Transports:"},
      .row{secondary},
      .value{[](auto const &s)->int{return total(s.vehicle_kills);}},
    },
    report_row{
      .label{"Enemy Missiles:"},
      .row{secondary + 1},
      .value{[](auto const &s)->int{return total(s.kla_kills);}},
    },
    report_row{
      .label{"Number of Planes Lost:"},
      .row{19},
      .value{[](auto const &s)->int{return maths::word(s.aircraft_lost);}},
    },
    report_row{
      .label{"Friendly Sites Lost:"},
      .row{20},
      .value{[](auto const &s)->int{return maths::word(s.friendly_destroyed);}},
    },
    report_row{
      .label{"Total Damage:"},
      .row{21},
      .value{[](auto const &s)->int{return maths::word(s.maximum_damage);}},
    },
  };
  auto const last{sortie.value_or(pilot ? pilot->last_sortie() : sortie_statistics{})};
  auto const best{pilot ? pilot->best_sortie() : sortie_statistics{}};
  auto const peaks{pilot ? pilot->individual_records() : sortie_statistics{}};
  for(auto const &field : fields) {
    int const row{field.row - (preliminary && field.row >= 19 ? 1 : 0)};
    screen.text(29 - static_cast<int>(field.label.size()), row, field.label, preliminary ? 12 : 6);
    if(columns) screen.text(33, row, std::format("{:>3}        {:>3}        {:>3}           {:>3}", field.value(last), field.value(peaks), field.value(best), field.value(career)));
    else screen.text(33, row, std::format("{:>4}", field.value(career)));
    if(preliminary) {
      screen.text(69, row, std::format("{:>3}", field.value(career)), 7);
      if(row >= 18) {
        screen.text(44, row, std::format("{:>3}", field.value(peaks)), 7);
        screen.text(55, row, std::format("{:>3}", field.value(best)), 7);
      }
    }
    if(preliminary && field.row < 19 && field.value(last) > (previous ? field.value(previous->individual_records()) : 0)) screen.text(37, row, "\x03", 12);
  }
  if(!columns) {
    std::array<unsigned int, 10> constexpr order{7, 8, 9, 4, 6, 3, 2, 1, 0, 5};
    std::array<std::string_view, 10> constexpr abbreviations{"PH", "BS", "SS", "GS", "DSC", "SA", "SC", "CM", "DFC", "TCM"};
    unsigned int lines{0}, largest{4};
    if(pilot) for(auto const &award : medal_definitions) {
      unsigned int const count{pilot->awards(award.kind)};
      lines += (count + 13) / 14;
      largest = std::max(largest, std::min(count, 14u));
    }
    int const column{56 - static_cast<int>(largest)};
    int row{lines > 12 ? 7 : 8};
    screen.text(column, row++, "DECORATIONS:", 2);
    if(pilot) for(auto const index : order) {
      auto const &award{medal_definitions[index]};
      unsigned int count{pilot->awards(award.kind)};
      if(count == 0) continue;
      screen.text(column, row, std::format("{:>3}: ", abbreviations[index]), 6);
      while(count != 0 && row < 23) {
        unsigned int const shown{std::min(count, 14u)};
        std::string glyphs;
        for(unsigned int i{0}; i != shown; ++i) glyphs.append(award.glyphs.data(), award.glyphs.size());
        screen.text(column + 5, row++, glyphs, award.colour);
        count -= shown;
      }
    }
  }
  if(preliminary) screen.text(24, 23, "Type almost any key to continue...", 14);
  else if(post) screen.text(24, 23, "Type almost any key to continue...", 13);
  else screen.text(columns && !global ? 16 : 7, 23, columns && !global ? "Type \x18\x19 for next/prev, or ESC, BS, or CR to exit"
    : global ? "Type \x18\x19 for next/prev, <INS> for medals, or ESC, BS, or CR to exit"
    : "Type \x18\x19 for next/prev, <INS> for stats, or ESC, BS, or CR to exit", 2);
}

} // anonymous namespace

void front_end::render_menus(render::framebuffer &target) const {
  /// Original menu composition is expressed in its 80-by-25 cells, including the retained underlay beneath dialogs
  graphics::interface_canvas canvas{target, menu_font, 16, 640, 400};
  graphics::menu_canvas screen{canvas};
  auto const *pilot{campaign.active_pilot()};
  auto const &worlds{saved_theatres};
  auto const active{campaign.active_theatre()};
  auto selected{[&](interface_page const menu){
    if(page == menu) return selection;
    auto const found{remembered_selection.find(menu)};
    return found == remembered_selection.end() ? (menu == interface_page::menu ? 2u : 0u) : found->second;
  }};
  auto options{[&](int const x, int const y, auto const &labels, unsigned int const highlighted){
    for(unsigned int n{0}; n != labels.size(); ++n) screen.option(x, y + static_cast<int>(n), labels[n], n == highlighted);
  }};
  screen.frame("Main Menu");
  screen.text(10, 6, "Use arrow keys or type highlighted letter to choose action:", 11);
  options(27, 8, main_choices, selected(interface_page::menu));
  screen.text(9, 21, "Active pilot:", 14); screen.text(24, 21, pilot_label(pilot));
  screen.text(9, 22, "Theater-of-Operations:", 14);
  screen.text(33, 22, active && *active < worlds.size() ? worlds[*active].title : "Training Mission");
  screen.text(9, 23, "Control:", 14); screen.text(19, 23, controls.joystick_device == -2 ? "Keyboard" : "Joystick");
  screen.text(53, 23, "Music", 14); screen.text(59, 23, music_on ? "On " : "Off", 11);
  interface_page const view{page == interface_page::menu_information || page == interface_page::notice ? information_return
    : page == interface_page::no_aircraft || page == interface_page::no_objectives ? interface_page::starting_airfield
    : page == interface_page::completed_theatre ? interface_page::theatre_delete
    : page == interface_page::unavailable_pilot || page == interface_page::resurrect_activate ? interface_page::pilots : page};
  bool const roster{view == interface_page::roster || view == interface_page::pilots || view == interface_page::pilot_name
    || view == interface_page::pilot_activate || view == interface_page::pilot_delete || view == interface_page::pilot_resurrect};
  bool const combat{view == interface_page::theatres || view == interface_page::new_theatre || view == interface_page::open_theatre
    || view == interface_page::theatre_delete || view == interface_page::theatre_records};
  bool const training_menu{view == interface_page::training || view == interface_page::training_counts || view == interface_page::training_wickedness || view == interface_page::training_value};
  if(roster || combat || view == interface_page::starting_airfield || view == interface_page::airfield_information) {
    screen.colours(1, 3, 6);
    screen.heading(roster ? "Duty Roster" : "Theater of Operations Menu");
    screen.panel(1, 5, 78, 23);
    for(int row{6}; row < 23; ++row) screen.text(47, row, "\xb3", 6);
    screen.text(47, 5, "\xc2", 6); screen.text(47, 23, "\xc1", 6);
    if(!roster) screen.text(47, 14, std::string{"\xc3"} + std::string(30, '\xc4') + '\xb4', 6);
  }
  if(view == interface_page::campaigns) {
    screen.colours(1, 3, 6);
    screen.heading("Campaigns and Worlds");
    screen.panel(8, 6, 71, 21);
    std::array<std::string_view, 9> constexpr choices{
      "\\Earth campaign", "\\Other Worlds campaign", "\\Practice and example worlds",
      "recorded \\Demonstration flights", "mission \\Builder",
      "mission builder \\Instructions", "Other Worlds gameplay \\Hints", "Other Worlds \\Title animation", "\\Return to main menu",
    };
    options(14, 8, choices, selection);
    screen.text(12, 18, "One pilot roster; independent careers in each campaign.", 14);
    screen.text(12, 20, "World selection: Left/Right switches groups.");
  } else if(view == interface_page::campaign_victory) {
    screen.colours(14, 4, 2);
    screen.heading("!!! CONGRATULATIONS AND EXCLAMATIONS !!!");
    screen.panel(2, 5, 77, 22);
    screen.text(8, 6, "General " + (pilot ? pilot->name() : std::string{"trainee"}), 15);
    if(pilot && pilot->resurrections() != 0) screen.text(8, 7, std::format("(With {} resurrection{},)", pilot->resurrections(), pilot->resurrections() == 1 ? "" : "s"));
    screen.text(8, 9, "having successfully completed all the relevant");
    screen.text(8, 10, "Theaters-of-Operation,");
    screen.text(8, 11, "and thereby OBLITERATING the alien threat,");
    screen.text(8, 12, "is hereby declared...");
    screen.text(16, 14, "MASTER of Corncob Other Worlds", 15);
    if(pilot && pilot->resurrections() != 0) {
      screen.text(8, 17, "Now, don't you want to try to do it without");
      screen.text(8, 18, "resurrections?");
    } else {
      screen.text(8, 17, "There is no greater accomplishment in Corncob 3D.");
      screen.text(8, 18, "You have thus earned the utmost respect from");
      screen.text(8, 19, "all who risked their lives in defense of Humankind.");
    }
    screen.text(20, 21, "Type almost any key to continue...", 15);
  } else if(view == interface_page::demonstrations) {
    screen.colours(1, 3, 6);
    screen.heading("Recorded Demonstration Flights");
    screen.panel(8, 6, 71, 21);
    std::array<std::string_view, 4> constexpr choices{
      "\\1 Basic Mission", "\\2 Junky Corridor", "\\3 Vertical Mission", "\\Return to campaigns",
    };
    options(15, 9, choices, selection);
    screen.text(12, 16, "Original recorded aircraft motion and weapon events.", 14);
    screen.text(12, 18, "Escape stops playback; demonstrations do not affect careers.");
  } else if(roster) {
    screen.text(59, 6, "Pilots:");
    screen.text(47, 7, std::string{"\xc3"} + std::string(30, '\xc4') + '\xb4', 6);
    screen.text(4, 7, "Use arrow keys or type highlighted letter");
    screen.text(12, 8, "to choose action:");
    options(10, 10, roster_choices, selected(interface_page::roster));
    screen.text(3, 21, "Active pilot:  " + pilot_label(pilot, true));
    unsigned int const first{view == interface_page::pilots && selection >= 14 ? selection - 13 : 0};
    auto const pilots{campaign.pilots()};
    for(unsigned int n{first}; n < pilots.size() && n < first + 14; ++n) {
      int const row{8 + static_cast<int>(n - first)};
      if(pilots[n].selected()) screen.text(48, row, "\x10");
      screen.text(49, row, pilot_label(&pilots[n], true).substr(0, 28), view == interface_page::pilots && n == selection ? 15 : 1);
    }
    if(view == interface_page::pilots || view == interface_page::pilot_delete || view == interface_page::pilot_resurrect) {
      unsigned int const chosen_index{view == interface_page::pilots ? selection : pilot_index};
      std::string_view const action{pilot_action == roster_action::activate ? "activate" : pilot_action == roster_action::remove ? "delete"
        : pilot_action == roster_action::rename ? "rename" : "resurrect"};
      screen.colours(1, 7, 4);
      screen.heading(pilot_action == roster_action::activate ? "Choose Active Pilot" : "Choose Pilot to " + std::string{action});
      screen.panel(2, 6, 45, 21);
      std::string const instruction{"Select pilot with \x18\x19, <CR> to " + std::string{action}};
      screen.text(24 - static_cast<int>(instruction.size()) / 2, 7, instruction, 4);
      auto const &chosen{pilots[chosen_index]};
      auto const totals{chosen.career()}, peak{chosen.individual_records()}, best{chosen.best_sortie()};
      std::string const label{"Pilot:  " + pilot_label(&chosen, true)};
      screen.text(24 - static_cast<int>(label.size()) / 2, 9, label);
      auto const condition{chosen.condition(report_time())};
      std::string const missions{std::format("{} mission{} {}", chosen.sorties(), chosen.sorties() == 1 ? "" : "s",
        condition == pilot_condition::captured ? "(captured)" : condition == pilot_condition::killed ? "(K.I.A.)"
        : condition == pilot_condition::stockade ? "(stockade)" : chosen.selected() ? "(active)" : "(ready for action)")};
      screen.text(24 - static_cast<int>(missions.size()) / 2, 11, missions);
      screen.text(4, 12, "Mission Objectives = " + std::to_string(totals.objectives_completed));
      screen.text(4, 13, "Total points = " + std::to_string(chosen.career_score()));
      auto primary{[](sortie_statistics const &s){return maths::word(s.objectives_completed + s.resource_kills + total(s.aaa_kills) + total(s.site_kills) + total(s.orb_kills));}};
      auto other{[](sortie_statistics const &s){return maths::word(total(s.kla_kills) + total(s.vehicle_kills));}};
      screen.text(4, 14, "Primary Kills = " + std::to_string(primary(totals)));
      screen.text(4, 15, "Other Kills = " + std::to_string(other(totals)));
      screen.text(4, 17, "Combined best-single-mission:");
      screen.text(5, 18, std::format("{} primary kills \xc4\xc4 {} other kills", primary(peak), other(peak)));
      screen.text(4, 19, "Best mission score = " + std::to_string(chosen.best_sortie_score()) + ":");
      screen.text(5, 20, std::format("{} primary kills \xc4\xc4 {} other kills", primary(best), other(best)));
      screen.colours(1, 3, 6);
      screen.text(49, 8 + static_cast<int>(chosen_index - first), pilot_label(&chosen, true).substr(0, 28), 11, 1);
    }
    if(view == interface_page::pilot_name || view == interface_page::pilot_activate) {
      screen.colours(4, 7, 5, 4);
      screen.heading(renaming ? "Rename Pilot" : "Create New Pilot");
      screen.panel(3, 9, 49, 16);
      screen.text(4, 10, renaming ? "Enter new name for " + campaign.pilots()[pilot_index].name() : "Enter name for new pilot.");
      screen.text(4, 11, "(Maximum of 20 characters)");
      screen.text(6, 12, entered_name + "_", 4);
      if(view == interface_page::pilot_activate) screen.rich_text(4, 14, "Activate " + entered_name + "? [\\Yes or \\No]");
    }
    if(view == interface_page::pilot_delete || view == interface_page::pilot_resurrect) {
      screen.colours(14, 6, 5);
      screen.heading(view == interface_page::pilot_delete ? "Confirm Deletion" : "Confirm Resurrection");
      screen.panel(3, 9, 59, 15);
      screen.text(5, 11, std::string{"Type <ENTER> to "} + (view == interface_page::pilot_delete ? "delete " : "resurrect ") + pilot_label(&campaign.pilots()[pilot_index], true), 14);
      screen.text(5, 12, " Almost anything else cancels...");
    }
  } else if(combat) {
    screen.text(4, 6, "Current Pilot:"); screen.text(19, 6, pilot_label(pilot, true), 14);
    screen.text(4, 7, "Active Theater :");
    screen.text(7, 8, active && *active < worlds.size() ? worlds[*active].title : "none", 14);
    screen.text(4, 10, "Choose with arrow keys or letter", 15);
    options(4, 13, combat_choices, selected(interface_page::theatres));
    screen.text(36, 18, combat_invulnerable ? "(Inv)" : "(Std)");
    screen.text(33, 19, controls.beginner_flight ? "(Beginner)" : "(Advanced)");
    screen.text(36, 20, comments_enabled ? "(On) " : "(Off)");
    screen.text(49, 6, "Theaters-of-Operation:", 14);
    auto const catalog{world::theatres(group)};
    unsigned int const first_world{view == interface_page::new_theatre && selection >= 6 ? selection - 5 : 0};
    for(unsigned int i{first_world}; i < catalog.size() && i < first_world + 6; ++i) {
      int const row{7 + static_cast<int>(i - first_world)};
      screen.text(49, row, catalog[i].required ? "\xfe" : " ", 14);
      screen.text(50, row, catalog[i].title.substr(0, 27), view == interface_page::new_theatre && i == selection ? 15 : 1);
    }
    if(catalog.size() > 6) screen.text(50, 13, std::format("{}-{} of {}  \x18\x19 scroll", first_world + 1, std::min(first_world + 6, static_cast<unsigned int>(catalog.size())), catalog.size()), 14);
    screen.text(47, 14, std::string{"\xc3"} + std::string(30, '\xc4') + '\xb4', 6);
    screen.text(49, 15, "Your Open Theaters:", 14);
    unsigned int const first{selection > 5 && view != interface_page::theatres && view != interface_page::new_theatre ? selection - 5 : 0};
    for(unsigned int i{first}; i < worlds.size() && i < first + 6; ++i) {
      int const row{16 + static_cast<int>(i - first)};
      if(active && i == *active) screen.text(49, row, "\x10");
      screen.text(50, row, worlds[i].title.substr(0, 27) + (worlds[i].objectives_remaining == 0 ? "\x03" : ""), view != interface_page::theatres && view != interface_page::new_theatre && i == selection ? 15 : 1);
    }
    if(view == interface_page::new_theatre) {
      screen.colours(4, 7, 4);
      screen.heading("Choose a Theater of Operations");
      screen.colours(1, 7, 4);
      screen.panel(2, 6, 45, 21);
      screen.text(4, 7, "Choose desired theater of operations with", 4);
      screen.text(4, 8, "\x18\x19.  Select theater of operations with", 4);
      screen.text(4, 9, "<CR> or abort with <BS> or <ESC>.", 4);
      screen.text(2, 10, std::string{"\xc3"} + std::string(42, '\xc4') + '\xb4', 4);
      screen.text(24 - static_cast<int>(catalog[selection].title.size()) / 2, 11, catalog[selection].title, 14);
      auto const bytes{world::theatre_briefing(catalog[selection].name)};
      std::istringstream text{std::string{reinterpret_cast<char const*>(bytes.data()), bytes.size()}};
      std::string line;
      std::getline(text, line);
      if(line.starts_with("X ")) std::getline(text, line);
      screen.text(4, 12, "Left/Right groups; B: edit a fresh copy", 4);
      for(int row{13}; row <= 20 && std::getline(text, line) && line.find('\f') == std::string::npos; ++row) {
        if(!line.empty() && line.back() == '\r') line.pop_back();
        screen.rich_text(4, row, line, 1, 40);
      }
    } else if(view != interface_page::theatres && selection < worlds.size()) {
      auto const &chosen{worlds[selection]};
      auto const &record{chosen.pilot};
      auto const totals{record.career()};
      screen.colours(1, 7, 4);
      screen.heading(view == interface_page::open_theatre ? "Activate an Open Theater" : view == interface_page::theatre_delete ? "Delete a Theater" : "View Status of a Theater");
      screen.panel(2, 6, 45, 21);
      screen.text(4, 7, "Select theater with \x18\x19, <CR> to select", 4);
      screen.text(24 - static_cast<int>(chosen.title.size() + 5) / 2, 9, chosen.title + " T.O.");
      screen.text(4, 10, std::format("{} misn{}.  {} objctvs killed, {} remain.", record.sorties(), record.sorties() == 1 ? "" : "s", totals.objectives_completed, chosen.objectives_remaining));
      screen.text(4, 11, chosen.stored_aircraft == 0 ? "There are no planes remaining in storage." : std::format("There are {} plane{} remaining in storage.", chosen.stored_aircraft, chosen.stored_aircraft == 1 ? "" : "s"));
      screen.text(4, 13, "Total points = " + std::to_string(record.career_score()));
      screen.text(4, 14, "Primary Kills = " + std::to_string(totals.objectives_completed + totals.resource_kills + total(totals.aaa_kills) + total(totals.site_kills) + total(totals.orb_kills)));
      screen.text(4, 15, "Other Kills = " + std::to_string(total(totals.kla_kills) + total(totals.vehicle_kills)));
      screen.text(4, 17, "Combined best-single-mission:");
      auto primary{[](sortie_statistics const &s){return maths::word(s.objectives_completed + s.resource_kills + total(s.aaa_kills) + total(s.site_kills) + total(s.orb_kills));}};
      auto secondary{[](sortie_statistics const &s){return maths::word(total(s.kla_kills) + total(s.vehicle_kills));}};
      auto const peaks{record.individual_records()}, best{record.best_sortie()};
      screen.text(5, 18, std::format("{} primary kills \xc4\xc4 {} other kills", primary(peaks), secondary(peaks)));
      screen.text(4, 19, "Best mission score = " + std::to_string(record.best_sortie_score()) + ":");
      screen.text(5, 20, std::format("{} primary kills \xc4\xc4 {} other kills", primary(best), secondary(best)));
      if(view == interface_page::theatre_records && detailed_records) report(screen, &record, true, "CAREER REPORT:  " + chosen.title + " Theater of Operations");
    }
  } else if(training_menu) {
    screen.colours(1, 3, 6);
    screen.heading("Training Missions");
    screen.panel(2, 5, 76, 22);
    screen.text(22, 6, "Use arrow keys or type highlighted");
    screen.text(26, 7, "letter to choose action:");
    options(24, 9, training_choices, selected(interface_page::training));
    screen.text(15, 17, "Current choices:", 14);
    screen.text(18, 18, std::string{"Plane:  "} + (training.invincible ? "Invulnerable" : "Standard"));
    auto strength{[](int const q){return q < 0 ? std::string{"random spread of strength"} : "strength " + std::to_string(q + 1);}};
    screen.text(18, 19, std::to_string(training.kla.count) + " missile sites:"); screen.text(43, 19, strength(training.kla.quality));
    screen.text(18, 20, std::to_string(training.aaa.count) + " AAA sites:"); screen.text(43, 20, strength(training.aaa.quality));
    screen.text(18, 21, std::to_string(training.vehicles.count) + " ground vehicle sites:"); screen.text(43, 21, strength(training.vehicles.quality));
    bool const editor{view == interface_page::training_value};
    bool const quality{view == interface_page::training_wickedness || (editor && training_editor == interface_page::training_wickedness)};
    if(view != interface_page::training) {
      int const left{quality ? 14 : 9}, right{quality ? 65 : 68};
      screen.colours(7, 4, 2);
      screen.heading(quality ? "Modify Wickedness of Enemies" : "Choose Number of Enemies");
      screen.panel(left, 8, right, 19);
      screen.text(left + 2, 9, "Use arrow keys or type highlighted");
      screen.text(left + 3, 10, "letter to choose action:");
      std::array<std::string, 4> const labels{quality ? "modify enemy \\Missile wickedness" : "change number of \\Missile sites",
        quality ? "modify enemy \\Ground vehicle" : "change number of \\Ground-Vehicle sites",
        quality ? "modify \\AAA wickedness" : "change number of \\AAA sites", "\\Return to previous menu"};
      options(left + 4, 12, labels, editor ? training_field : selection);
      for(unsigned int n{0}; n != 3; ++n) {
        auto const &defence{n == 0 ? training.kla : n == 1 ? training.vehicles : training.aaa};
        screen.text(52, 12 + static_cast<int>(n), "currently " + std::to_string(quality ? defence.quality + 1 : static_cast<int>(defence.count)), n == (editor ? training_field : selection) ? 15 : 7);
      }
      if(editor) {
        auto const &defence{training_field == 0 ? training.kla : training_field == 1 ? training.vehicles : training.aaa};
        std::string const kind{training_field == 0 ? "Missile" : training_field == 1 ? "Ground Vehicle" : "AAA"};
        screen.colours(4, 7, 5, 4);
        screen.heading(quality ? "Modify " + kind + " Wickedness" : "Choose # of " + kind + " Sites");
        screen.panel(left + 2, 9, right - 3, 17);
        screen.text(left + 4, 11, quality ? kind + " wickedness is currently " + std::to_string(defence.quality + 1) + "."
          : "The number of " + kind + " sites is currently " + std::to_string(defence.count) + ".");
        if(quality) {
          screen.text(left + 5, 12, "Values of 1-8 mean increasing wickedness.");
          screen.text(left + 5, 13, "A value of 0 will choose a random");
          screen.text(left + 5, 14, "spread of wickedness.");
        }
        screen.text(left + 4, quality ? 16 : 13, std::string{"Enter new value [0-"} + (quality ? "8]  " : "25]  ") + numeric_input + "_", 4);
        if(!feedback.empty()) screen.text(left + 4, 15, feedback, 12);
      }
    }
  } else if(view == interface_page::mission_briefing || view == interface_page::starting_airfield || view == interface_page::airfield_information) {
    screen.colours(view == interface_page::airfield_information ? 7 : 1, view == interface_page::airfield_information ? 6 : 3, view == interface_page::airfield_information ? 14 : view == interface_page::starting_airfield ? 5 : 6, 15);
    screen.heading(view == interface_page::mission_briefing ? "Mission Briefing" : view == interface_page::starting_airfield ? "Your Starting Airfield" : "Detailed Info");
    screen.panel(1, 5, 77, 22);
    if(view == interface_page::mission_briefing) {
      std::string const greeting{pilot_label(pilot) + ", prepare for your mission!"};
      screen.text(39 - static_cast<int>(greeting.size()) / 2, 6, greeting, 14);
      screen.text(1, 7, std::string{"\xc3"} + std::string(75, '\xc4') + '\xb4', 6);
      screen.text(31, 8, "Training Mission:", 14);
      screen.text(5, 9, "Your objective is to destroy the flying saucer and to take out");
      screen.text(5, 10, "as many enemy fuel dumps and ground sites as possible.");
      screen.text(5, 12, "Latest intelligence reports enemy strengths as follows:");
      auto strength{[](int const q){return q < 0 ? std::string{"random spread of strength"} : "strength " + std::to_string(q + 1);}};
      screen.text(15, 13, std::to_string(training.kla.count) + " Missile sites:"); screen.text(39, 13, strength(training.kla.quality));
      screen.text(15, 14, std::to_string(training.aaa.count) + " AAA sites:"); screen.text(39, 14, strength(training.aaa.quality));
      screen.text(15, 15, std::to_string(training.vehicles.count) + " Ground-Vehicle sites:"); screen.text(39, 15, strength(training.vehicles.quality));
      screen.text(15, 16, "1 Enemy Saucers:"); screen.text(39, 16, "random spread of strength");
      screen.text(3, 19, "Note:  Training missions are much harder than the combat missions.");
      screen.text(21, 21, "Type <CR> to continue, <ESC> to abort", 14);
    } else if(view == interface_page::starting_airfield) {
      bool const multiple{prepared_world && world::multiple_airfields(*prepared_world)};
      screen.text(multiple ? 6 : 11, 6, multiple ? "Choose an airfield with arrows; Enter flies, I shows details." : "There is only one airfield in this Theater-of-Operation.", 14);
      if(multiple) {
        for(unsigned int field{0}; field != world::airfield_count; ++field) {
          int const row{8 + static_cast<int>(field)};
          auto const status{resources::survey_airfield(*prepared_world, field)};
          screen.option(5, row, world::airfield_names[field], field == selection);
          screen.text(47, row, std::format("{} planes  {} MO", status.planes, status.objectives), 15);
        }
        screen.text(6, 19, "Escape returns without starting a flight.", 14);
      } else {
        screen.text(6, 7, "Type <CR> to fly, `i' for detailed info, or <ESC> to abort mission.", 14);
        screen.text(34, 13, "RDU ^international", 15); screen.text(39, 14, "Airport", 15);
        auto const status{prepared_world ? resources::survey_airfield(*prepared_world) : resources::airfield_status{}};
        screen.text(35, 15, std::format("({} planes  {} MO)", status.planes, status.objectives), 15);
      }
      screen.text(25, 21, "MO = Mission Objectives remaining", 1);
    } else {
      screen.colours(7, 6, 7, 15);
      bool highlighted{false};
      for(unsigned int i{0}; i < scroll; ++i) for(unsigned int n{0}; n < document_lines[i].size(); ++n) {
        if(document_lines[i][n] == '\\' && n + 1 < document_lines[i].size()) {
          char const marker{document_lines[i][++n]};
          if(marker == '(') highlighted = true;
          else if(marker == ')') highlighted = false;
        }
      }
      for(unsigned int i{0}; i < 15 && i + scroll < document_lines.size(); ++i)
        screen.rich_text(3, 6 + static_cast<int>(i), document_lines[i + scroll], 7, 73, highlighted);
      if(document_lines.size() > 15) {
        for(int row{6}; row < 21; ++row) screen.text(76, row, "\xb1", 7);
        unsigned int const height{std::max(1u, (225u + static_cast<unsigned int>(document_lines.size()) - 1) / static_cast<unsigned int>(document_lines.size()))};
        unsigned int const top{scroll * (15u - height) / static_cast<unsigned int>(document_lines.size() - 15)};
        for(unsigned int row{0}; row < height; ++row) screen.text(76, 6 + static_cast<int>(top + row), " ", 7, 7);
      }
      screen.text(3, 21, std::string(74, ' '), 15, 0);
      screen.text(16, 21, "Type \x18\x19 <P-UP> <P-DN> etc, or <CR> to return...", 15, 0);
    }
  } else if(view == interface_page::control_setup) {
    screen.colours(1, 3, 6);
    screen.heading("Redux Options and Controls");
    screen.panel(5, 5, 74, 22);
    auto const &s{settings.scene};
    std::array<std::string, 11> const labels{
      "Configure key and mouse bindings",
      std::string{"Flight input: "} + (controls.joystick_device == -2 ? "Keyboard" : "Joystick (automatic)"),
      std::string{"Flight model: "} + (controls.beginner_flight ? "Beginner" : "Advanced"),
      std::format("Horizontal FOV: {:.0f} degrees", s.horizontal_fov),
      s.draw_distance == 0 ? "Draw distance: Unlimited" : std::format("Draw distance: {:.0f} km", s.draw_distance),
      std::format("Ground detail: {} points", s.ground_points),
      settings.window_resolution ? "Resolution: Match window" : std::format("Resolution: {} x {}", s.resolution.width, s.resolution.height),
      std::format("Mouse sensitivity: {:.0f}", settings.mouse_sensitivity),
      std::string{"Invert mouse look: "} + (settings.invert_mouse ? "Yes" : "No"),
      std::format("Ground detail range: {:.2g} x DOS", s.ground_range),
      "Back",
    };
    options(10, 8, labels, selection);
    screen.text(9, 19, "Left/Right changes values; Enter selects; Escape returns.", 15);
    screen.text(9, 20, "Settings are saved automatically.", 14);
  } else if(view == interface_page::bindings) {
    screen.colours(1, 3, 6);
    screen.heading("Control Bindings");
    screen.panel(3, 4, 76, 23);
    unsigned int const count{static_cast<unsigned int>(settings.bindings.size())};
    unsigned int const first{selection < count ? (selection / 12) * 12 : (count - 1) / 12 * 12};
    screen.text(6, 5, binding_capture ? "Press a key or mouse button; Escape cancels." : "Enter: rebind   Delete while capturing: clear", 15);
    for(unsigned int n{first}; n < std::min(first + 12, count); ++n) {
      auto const &b{settings.bindings[n]};
      std::string const scope{b.context == control_context::walking ? "Walk  " : b.context == control_context::flight ? "Flight" : "Both  "};
      screen.option(6, 7 + static_cast<int>(n - first), std::format("{}  {:28} {}", scope, b.name, binding_capture && n == selection ? "..." : binding_name(b)), n == selection);
    }
    screen.text(62, 6, std::format("{}-{}/{}", first + 1, std::min(first + 12, count), count), 14);
    for(unsigned int row{0}; row < 12; ++row) screen.text(73, 7 + static_cast<int>(row), row == first * 11 / count ? "#" : "|", 14);
    screen.option(6, 20, "Restore control defaults", selection == count);
    screen.option(6, 21, return_page == interface_page::control_setup ? "Back to options" : "Back", selection == count + 1);
  } else if(view == interface_page::high_score_menu || view == interface_page::score_delete || view == interface_page::score_delete_confirmation) {
    screen.colours(1, 3, 6);
    bool const deleting{view != interface_page::high_score_menu};
    screen.heading(deleting ? "Delete a Score List" : "High Score Menu");
    screen.panel(12, 5, 68, 19);
    screen.text(22, 8, "Use arrow keys or type highlighted");
    screen.text(24, 9, "letter to choose action:");
    if(deleting) options(22, 11, delete_choices, selected(interface_page::score_delete));
    else options(22, 11, score_choices, selected(interface_page::high_score_menu));
    if(view == interface_page::score_delete_confirmation) {
      screen.colours(4, 7, 5, 4);
      screen.heading("Confirm Deletion"); screen.panel(19, 9, 61, 15);
      screen.text(21, 11, "Type <ENTER> to delete high score list", 14);
      screen.text(21, 12, " Almost anything else cancels...");
    }
  } else if(view == interface_page::high_scores) {
    std::array<std::string_view, 3> constexpr titles{"High Scores", "Top Careers", "Fastest Careers"};
    screen.colours(1, 3, 6);
    screen.heading(titles[score_list]); screen.panel(2, 5, 76, 20);
    screen.text(4, 6, score_list == 0 ? "Name                       Score  Theater-of-Operations      Kills"
      : score_list == 1 ? "Name                 Total Score  Missions Resurrections     Kills"
      : "Name                 Total Score  Missions Resurrections   Points/Misn", 14);
    unsigned int count{0};
    auto const &tables{campaign.high_scores()};
    for(unsigned int i{0}; i != 10; ++i) {
      auto const name{score_list == 0 ? score_text(tables.best_missions()[i].name) : score_text((score_list == 1 ? tables.best_careers() : tables.fastest_careers())[i].name)};
      if(name.empty()) break;
      auto const &statistics{score_list == 0 ? tables.best_missions()[i].statistics : (score_list == 1 ? tables.best_careers() : tables.fastest_careers())[i].statistics};
      int const row{8 + static_cast<int>(count++)};
      bool const retired{score_list == 0 ? tables.best_missions()[i].retired != 0 : (score_list == 1 ? tables.best_careers() : tables.fastest_careers())[i].retired != 0};
      screen.text(4, row, name, 15);
      screen.text(4 + static_cast<int>(name.size()), row, retired ? "*" : " ", retired ? 14 : 15);
      int const primary{maths::word(statistics.resource_kills + statistics.objectives_completed + total(statistics.aaa_kills) + total(statistics.site_kills) + total(statistics.orb_kills))};
      int const secondary{maths::word(total(statistics.kla_kills) + total(statistics.vehicle_kills))};
      if(score_list == 0) {
        auto const &entry{tables.best_missions()[i]};
        screen.text(25, row, std::format("  {:>9}  {:<20}  {:>3}  {:>3}  {:>3}", entry.points, score_text(entry.theatre), maths::word(statistics.objectives_completed), primary, secondary), 15);
      } else {
        auto const &entry{(score_list == 1 ? tables.best_careers() : tables.fastest_careers())[i]};
        auto text{std::format("  {:>9}  {:<8} {:<8}", entry.points, maths::word(entry.sorties), maths::word(entry.resurrections))};
        if(score_list == 1) text += std::format("    {:>4} {:>5} {:>5}", maths::word(statistics.objectives_completed), primary, secondary);
        else text += std::format("   {:>16}", entry.sorties == 0 ? 0 : entry.points / maths::word(entry.sorties));
        screen.text(25, row, text, 15);
      }
    }
    screen.text(5, 9 + static_cast<int>(count), "Type almost any key to continue...");
  } else if(view == interface_page::records || view == interface_page::medals) {
    report(screen, &campaign.pilots()[pilot_index], detailed_records, "CAREER REPORT");
  } else if(view == interface_page::current_score) {
    /// ISCORE evaluates a temporary record as though the pilot had just landed, without saving that speculative sortie
    auto result{latest_result.value_or(sortie_statistics{})};
    result.mark(sortie_status::home);
    result.mark(sortie_status::landed);
    auto const *record{pilot};
    std::string title{"PRELIMINARY REPORT"};
    if(!training_report && active && *active < worlds.size()) {
      record = &worlds[*active].pilot;
      title += ":  " + worlds[*active].title + " Theater of Operations";
    }
    pilot_record preview{record ? *record : pilot_record{}};
    preview.finish_sortie(result, score_context::flight_report);
    report(screen, &preview, true, title, result, record);
  } else if(view == interface_page::sortie_summary || view == interface_page::career_announcement) {
    pilot_record preview{mission_prior};
    preview.finish_sortie(latest_result.value_or(sortie_statistics{}));
    report(screen, &preview, true, mission_report_title, latest_result, &mission_prior);
  } else if(view == interface_page::post_mission_report) {
    report(screen, returned_pilot ? &*returned_pilot : nullptr, false, "POST-MISSION CAREER REPORT");
  } else if((view == interface_page::menu && session_running) || view == interface_page::quit_confirmation) {
    if(view == interface_page::quit_confirmation) {
      std::array<std::string_view, 2> constexpr choices{"Yes", "No - return"}; draw_menu(canvas, "End this sortie?", choices);
    } else {
      std::array<std::string_view, 4> constexpr choices{"Resume flight", "Options and controls", "Control bindings", "End current sortie"}; draw_menu(canvas, "Main menu", choices);
    }
  }
  if(page == interface_page::documentation || page == interface_page::registration || page == interface_page::menu_information) {
    bool const context{page == interface_page::menu_information};
    int const left{context ? 2 : 0}, top{context ? 5 : 0}, right{context ? 76 : 79}, bottom{context ? 22 : 24};
    screen.colours(7, page == interface_page::documentation ? 8 : 4, page == interface_page::documentation ? 9 : 2, 15);
    if(context) {
      std::array<std::string_view, 9> constexpr titles{"", "Main Menu Information", "Duty Roster Menu Information", "Combat Missions Information", "", "Keyboard/Joystick Information", "Training Missions Information", "High Score Menu Information", "Delete a High Score List"};
      screen.heading(titles.at(information_section));
    } else canvas.clear();
    screen.panel(left, top, right, bottom, 16, false, context);
    unsigned int const visible{static_cast<unsigned int>(bottom - top - 2)};
    for(unsigned int i{0}; i != visible && i + scroll < document_lines.size(); ++i)
      screen.rich_text(left + 1, top + 1 + static_cast<int>(i), document_lines[i + scroll], 7, static_cast<unsigned int>(right - left - 3));
    if(document_lines.size() > visible) {
      for(int y{top + 1}; y < bottom - 1; ++y) screen.text(right - 1, y, "\xb1", 7);
      unsigned int const thumb_height{std::max(1u, (visible * visible + static_cast<unsigned int>(document_lines.size()) - 1) / static_cast<unsigned int>(document_lines.size()))};
      int const thumb{top + 1 + static_cast<int>(scroll * (visible - thumb_height) / (document_lines.size() - visible))};
      for(unsigned int i{0}; i != thumb_height; ++i) screen.text(right - 1, thumb + static_cast<int>(i), " ", 7, 7);
    }
    canvas.rectangle((left + 1) * 8, (bottom - 1) * 16, right * 8, bottom * 16, render::rgba_pixel{});
    if(page == interface_page::documentation && document_chapter == 0) {
      if(numeric_input.empty()) screen.text(4, 23, "Choose a chapter number for more info, <CR> to continue or <ESC> to exit", 15, 0);
      else {
        // MOAG replaces the entire instruction row with its grey chapter-number input field.
        screen.text(1, 23, std::string(78, ' '), 8, 7);
        screen.text(4, 23, "Enter a Chapter number:  " + numeric_input + "_", 8, 7);
      }
    } else screen.text(context ? 13 : 14, bottom - 1, "Type \x18 \x19 <P-UP> <P-DN> etc..., <CR> or <ESC> to exit", 15, 0);
  }
  if(page == interface_page::unavailable_pilot || page == interface_page::resurrect_activate) {
    auto const &chosen{campaign.pilots()[pilot_index]};
    auto const condition{chosen.condition(report_time())};
    bool const stockade{condition == pilot_condition::stockade};
    bool const unnecessary{pilot_action == roster_action::resurrect && condition == pilot_condition::ready};
    screen.colours(4, 7, 12);
    screen.heading(page == interface_page::resurrect_activate ? "Activate Pilot" : unnecessary ? "Pilot doesn't Need Resurrection"
      : stockade && pilot_action == roster_action::resurrect ? "Pilot is in the Stockade" : "Pilot Cannot be Activated");
    screen.panel(6, 12, 70, 17, 12);
    if(page == interface_page::resurrect_activate) {
      screen.text(8, 14, "Activate " + pilot_label(&chosen) + "? [Yes or No]");
    } else if(unnecessary) {
      screen.text(8, 13, pilot_label(&chosen) + " is not dead or captured.");
      screen.text(8, 14, "You can only resurrect dead or captured pilots.");
      screen.text(9, 16, "Type almost any key to continue...");
    } else {
      screen.text(8, 13, pilot_label(&chosen) + " has been " + (condition == pilot_condition::captured
        ? "captured by the enemy." : stockade ? "confined to the stockade." : "killed in action."));
      if(stockade) {
        screen.text(8, 14, "Release is scheduled for " + release_date(chosen.suspended_until()) + ".");
        screen.text(9, 16, "Type almost any key to continue...");
      } else screen.text(8, 15, "Do you want to resurrect the pilot?  (Y or N)");
    }
  }
  if(page == interface_page::completed_theatre) {
    screen.colours(5, 6, 14);
    screen.heading("Theater is Complete");
    screen.panel(6, 9, 55, 16);
    screen.text(8, 10, "Warning!  theater " + worlds[selection].title);
    screen.text(8, 11, "has already been completed!  Deleted theaters");
    screen.text(8, 12, "can not be counted toward Combat Master status.");
    screen.text(8, 14, "Are you sure you want to delete this theater?");
    screen.text(8, 15, "Type `y' to confirm, anything else cancels ");
  }
  if(page == interface_page::no_objectives) {
    screen.colours(4, 7, 5);
    screen.heading("Theater is Complete");
    screen.panel(14, 9, 63, 15);
    screen.text(16, 10, "There are no mission objectives remaining in");
    screen.text(16, 11, "this Theater of Operations.");
    screen.text(20, 13, "Type <ESC> to abort,");
    screen.text(21, 14, "or almost anything else to continue...");
  }
  if(page == interface_page::career_announcement) {
    auto const &announcement{announcements.at(announcement_index)};
    bool const promotion{announcement.promotion.has_value()};
    screen.colours(promotion ? 1 : 7, promotion ? 3 : 4, promotion ? 1 : 7);
    screen.panel(6, 7, 71, 17);
    int row{9};
    for(auto const &line : announcement.lines) {
      screen.text(((row == 9 ? 80 : 78) - static_cast<int>(line.size())) / 2, row, line);
      row += row == 9 ? 2 : 1;
    }
    if(announcement.promotion) {
      std::string const banner{"\xdb\xdb\xb2\xb2\xb1\xb1\xb0\xb0 " + std::string{rank_name(*announcement.promotion)} + " \xb0\xb0\xb1\xb1\xb2\xb2\xdb\xdb"};
      screen.text((78 - static_cast<int>(banner.size())) / 2, 14, banner, 14);
    }
    if(announcement.decoration) {
      auto const &award{medal_definitions[std::to_underlying(*announcement.decoration) - std::to_underlying(medal::purple_heart)]};
      int const left{(78 - static_cast<int>(award.name.size()) - 5) / 2};
      screen.text(left, 14, award.name, 14);
      screen.text(left + static_cast<int>(award.name.size()) + 1, 14, " " + std::string{award.glyphs.data(), award.glyphs.size()} + " ", award.colour, 1);
    }
    screen.text(22, 16, "Type almost any key to continue...", promotion ? 1 : 7);
  }
  if(page == interface_page::no_aircraft) {
    unsigned int const reserves{active ? worlds[*active].stored_aircraft : 0};
    screen.colours(4, 7, 5);
    screen.heading("No Planes at this Airfield");
    screen.panel(8, 11, 69, reserves != 0 ? 19 : 17);
    screen.text(11, 12, "Warning!  There are no planes available at this airstrip.");
    int row{14};
    if(reserves != 0) {
      screen.text(19, 13, std::format("(However, you have {} plane{} in storage.)", reserves, reserves == 1 ? "" : "s"));
      screen.text(20, 15, "Type 'P' to pull a new plane from storage.");
      row = 16;
    }
    screen.text(18, row++, "Type <CR> to continue with the mission anyway.");
    screen.text(30, row++, "Type <ESC> to abort.");
    screen.text(17, row, "Type almost anything else to select a new tower.");
  }
  if(page == interface_page::notice) {
    // Wrap long file paths rather than losing the destination at the right edge of the dialog.
    unsigned int constexpr columns{61}, maximum_rows{19};
    std::vector<std::string> lines;
    std::istringstream text{feedback};
    std::string line;
    while(std::getline(text, line)) {
      if(line.empty()) lines.emplace_back();
      for(unsigned int start{0}; start < line.size(); start += columns) lines.push_back(line.substr(start, columns));
    }
    unsigned int const rows{std::min(static_cast<unsigned int>(lines.size()), maximum_rows)};
    int const height{std::max(8, static_cast<int>(rows) + 4)};
    int const top{std::min(9, 24 - height)}, bottom{top + height - 1};
    screen.heading("Information"); screen.panel(8, top, 72, bottom);
    for(unsigned int row{0}; row != rows; ++row) screen.text(10, top + 2 + static_cast<int>(row), lines[row]);
    screen.text(10, bottom - 1, "Type almost any key to continue...", 14);
  }
}

} // namespace corncob::game

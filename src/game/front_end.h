#pragma once

#include <map>
#include <optional>
#include <string>
#include "audio/music_cue.h"
#include "game/campaign.h"
#include "game/preferences.h"
#include "game/session.h"
#include "game/tower_briefing.h"
#include "graphics/interface.h"
#include "graphics/space_sequence.h"
#include "graphics/title_sequence.h"
#include "resources/cockpit_mask.h"
#include "resources/radio_messages.h"
#include "world/training.h"

namespace corncob::game {

enum class interface_command {
  any_key,
  up,
  down,
  previous,
  next,
  accept,
  cancel,
  help,
  page_up,
  page_down,
  backspace,
  details,
  home,
  end,
};
enum class interface_page {
  title,
  ending,
  story,
  menu,
  campaigns,
  demonstrations,
  campaign_victory,
  theatres,
  new_theatre,
  open_theatre,
  theatre_records,
  theatre_delete,
  training,
  training_counts,
  training_wickedness,
  training_value,
  control_setup,
  bindings,
  mission_briefing,
  starting_airfield,
  no_aircraft,
  no_objectives,
  completed_theatre,
  unavailable_pilot,
  resurrect_activate,
  career_announcement,
  post_mission_report,
  airfield_information,
  roster,
  pilots,
  pilot_name,
  pilot_delete,
  records,
  medals,
  sortie_summary,
  high_scores,
  high_score_menu,
  score_delete,
  score_delete_confirmation,
  menu_information,
  documentation,
  registration,
  notice,
  pilot_activate,
  pilot_resurrect,
  current_score,
  boss,
  radio,
  briefing,
  help,
  quit_confirmation,
  outcome,
  flight,
};
struct launch_request {
  std::string theatre;
  std::optional<world::training_setup> training{};
  bool invincible{false};
  bool comments{true};
  bool reserve_aircraft{false};
  unsigned int airfield{4};
  bool editing{false};
  std::string recording{};
};

struct control_setup {
  int joystick_device{-1};
  bool beginner_flight{true};
};

class front_end {
private:
  resources::collection const &assets;
  resources::collection expansion_assets;
  world::theatre_group group{world::theatre_group::earth};
  unsigned int starting_airfield{4};
  campaign_store &campaign;
  std::array<uint8_t, 4096> menu_font;
  enum class roster_action { activate, records, rename, resurrect, remove };
  roster_action pilot_action{roster_action::activate};
  unsigned int pilot_index{0};
  unsigned int score_list{0};
  std::optional<graphics::space_sequence> ending;
  std::optional<graphics::title_sequence> title;
  render::framebuffer radio_backdrop;
  std::optional<resources::radio_message> radio;
  bool map_after_radio{false};
  bool pending_map{false};
  bool pending_abort{false};
  std::string entered_name;
  std::string feedback;
  bool renaming{false};
  bool pending_finish{false};
  bool pending_campaign_victory{false};
  bool training_report{false};
  pilot_record mission_prior;
  std::optional<pilot_record> returned_pilot;
  std::string mission_report_title;
  world::training_setup training;
  control_setup controls;
  preferences settings;
  bool binding_capture{false};
  void adjust_setting(int direction);
  void save_settings();
  tower_briefing tower;
  interface_page boss_return{interface_page::flight};
  std::optional<world::mission_start> pending_relocation;
  std::optional<sortie_statistics> latest_result;
  struct career_announcement {
    std::vector<std::string> lines;
    std::optional<medal> decoration{};
    std::optional<pilot_rank> promotion{};
  };
  std::vector<career_announcement> announcements;
  unsigned int announcement_index{0};
  std::vector<resources::presentation_panel> stories;
  std::vector<resources::presentation_panel> outcomes;
  std::vector<resources::cockpit_mask> help_screens;
  interface_page page{interface_page::title};
  interface_page return_page{interface_page::menu};
  unsigned int selection{2};
  std::map<interface_page, unsigned int> remembered_selection;
  unsigned int training_field{0};
  interface_page training_editor{interface_page::training_counts};
  interface_page information_return{interface_page::menu};
  unsigned int information_section{1};
  unsigned int document_chapter{0};
  std::string numeric_input;
  bool detailed_records{false};
  bool combat_invulnerable{false};
  bool comments_enabled{true};
  bool automatic_centering{true};
  bool force_centering{false};
  std::vector<std::string> document_lines;
  unsigned int story_index{0};
  double story_intensity{0.0};
  bool advancing_story{false};
  unsigned int help_index{0};
  unsigned int scroll{0};
  unsigned int outcome_index{0};
  std::string selected_theatre;
  std::optional<launch_request> pending_launch;
  std::optional<launch_request> prepared_launch;
  std::optional<world::theatre> prepared_world;
  std::vector<saved_theatre> saved_theatres;
  bool quit_requested{false};
  bool session_running{false};
  bool music_on{true};

  resources::collection const &campaign_assets() const noexcept;
  void choose_group(world::theatre_group selected);
  void accept();
  void navigate(interface_page destination);
  void sync_pilot();
  void information(unsigned int section);
  std::string_view document_text(std::string_view name) const;
  void document(std::string_view text, unsigned int section);
  void notice(std::string message);
  void launch_combat();
  void finish_reports();
  void render_menus(render::framebuffer &target) const;
  unsigned int choice_count() const;
  void draw_menu(graphics::interface_canvas &canvas, std::string_view title, std::span<std::string_view const> choices) const;

public:
  front_end(resources::collection const &assets, campaign_store &campaign, bool skip_intro = false, bool start_in_flight = false, std::optional<control_setup> controls = std::nullopt, std::optional<bool> music_enabled = std::nullopt);
  preferences &options() noexcept;
  void set_fullscreen(bool enabled);
  bool capturing_binding() const noexcept;
  bool bind_input(int key, int mouse = -1);
  void wheel(int steps);
  control_setup const &flight_controls() const noexcept;
  audio::music_cue music() const noexcept;
  void advance(double seconds);
  void begin_ending(graphics::indexed_framebuffer const &backdrop, std::array<render::rgba_pixel, 16> const &colours);
  void character(uint32_t codepoint);
  bool editing_name() const noexcept;
  bool take_finish();
  bool take_abort();
  bool take_map();
  void abort_sortie();
  void leave_sortie();
  void show_radio(resources::radio_notice notice, render::framebuffer const &backdrop, bool enter_map = false);
  void finish_sortie(sortie_statistics const &result, pilot_record const *previous = nullptr, pilot_record const *previous_theatre = nullptr);
  void command(interface_command command);
  void begin_flight();
  void finish_editing();
  void show_menu();
  void show_intelligence(world::theatre const &world, unsigned int airfield = tower_briefing::central_airfield);
  void show_current_score(sortie_statistics const &result, bool training = false);
  void show_boss();
  std::optional<world::mission_start> take_relocation();
  void show_flight_help();
  void ask_to_end_sortie();
  void show_outcome(unsigned int index);
  void render(render::framebuffer &target) const;
  std::optional<launch_request> take_launch();
  bool in_flight() const noexcept;
  bool quitting() const noexcept;
  interface_page current_page() const noexcept;
};

} // namespace corncob::game

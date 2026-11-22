#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include "platform/storage.h"
#include "game/campaign.h"
#include "game/session.h"
#include "resources/assets.h"
#include "resources/binary_reader.h"

namespace {

void require(bool const condition, char const *const message) {
  /// Keep persistence checks active in optimised builds
  if(!condition) throw std::runtime_error{message};
}

} // anonymous namespace

int main(int const argc, char const *const argv[]) {
  /// Persist a named career and changed original world, reopen it, and keep different pilots' worlds separate
  if(argc != 3) throw std::invalid_argument{"campaign_checks requires its dedicated scratch directory"};
  std::filesystem::path const directory{argv[1]};
  if(directory.filename() != "campaign-checks") throw std::invalid_argument{"campaign check directory must be named campaign-checks"};
  std::filesystem::remove_all(directory);
  namespace game = corncob::game;
  namespace world = corncob::world;
  auto theatre{world::decode_theatre(corncob::resources::morning_asset())};
  auto orb{std::ranges::find_if(theatre.objects, [](auto const &object){return object.type == world::object_type::generator_orb;})};
  require(orb != theatre.objects.end(), "reference theatre lacks a generator orb");
  orb->type = world::object_type::destroyed_generator_orb;
  orb->flags = 0x211;
  {
    /// Original pilot references remain authoritative when MOAG has left the old owner name in its THT header
    auto const pilot{corncob::platform::read_binary(std::filesystem::path{argv[2]} / "pilot_theatre.bin")};
    corncob::platform::write_binary(directory / "PILOT.SCR", pilot);
    std::vector<uint8_t> saved(671);
    std::string const stem{"morning"};
    std::ranges::copy(stem, saved.begin() + 28);
    game::pilot_record const stale{"NativeTest"};
    std::ranges::copy(stale.native_data(), saved.begin() + 48);
    auto const objects{world::encode_theatre(theatre)};
    saved.insert(saved.end(), objects.begin(), objects.end());
    corncob::platform::write_binary(directory / "THT/1.THT", saved);
    game::campaign_store original{directory};
    require(original.active_pilot() && original.active_pilot()->name() != stale.name(), "original stale-name scenario lost its rename");
    require(original.load_world("morning").has_value(), "original numbered reference did not recover a renamed pilot's theatre");
    original.rename_pilot(0, "Imported pilot");
    require(original.load_world("morning").has_value(), "renaming orphaned an imported original theatre");
    std::filesystem::remove_all(directory);
  }
  {
    game::campaign_store campaign{directory};
    require(campaign.active_pilot() == nullptr, "empty roster did not begin with disposable trainee");
    campaign.create_pilot("Native pilot");
    require(campaign.active_pilot()->name() == "Native pilot", "new pilot was not active");
    game::sortie_statistics result{
      .objectives_completed{1},
    };
    result.mark(game::sortie_status::home);
    result.mark(game::sortie_status::landed);
    campaign.finish_sortie("morning", theatre, result);
  }
  {
    game::campaign_store campaign{directory};
    require(campaign.active_pilot() != nullptr && campaign.active_pilot()->rank() == game::pilot_rank::first_lieutenant, "saved pilot progression was lost on restart");
    require(campaign.active_pilot()->career_score() == 1875, "saved career score changed");
    require(campaign.high_scores().best_missions().front().points == 1875, "saved high score was lost on restart");
    auto const loaded{campaign.load_world("morning")};
    require(loaded.has_value(), "changed theatre was not saved");
    require(std::ranges::any_of(loaded->objects, [](auto const &object){return object.type == world::object_type::destroyed_generator_orb && (object.flags & 0x200u) != 0;}), "destroyed objective reverted after loading");
    auto const career_before{std::vector<uint8_t>{campaign.active_pilot()->native_data().begin(), campaign.active_pilot()->native_data().end()}};
    auto const scores_before{campaign.high_scores().encode()};
    for(auto const excluded : {game::sortie_status::invincible, game::sortie_status::aborted}) {
      game::sortie_statistics unrecorded{
        .objectives_completed{100},
      };
      unrecorded.mark(excluded);
      unrecorded.mark(game::sortie_status::killed);
      campaign.finish_sortie("morning", theatre, unrecorded);
      require(std::ranges::equal(campaign.active_pilot()->native_data(), career_before), "unrecorded sortie changed a saved career");
      require(campaign.high_scores().encode() == scores_before, "unrecorded sortie entered a high-score table");
    }
    campaign.rename_pilot(0, "Renamed pilot");
    require(campaign.load_world("morning").has_value(), "renaming orphaned the pilot's theatre");
    campaign.create_pilot("Other pilot");
    require(!campaign.load_world("morning"), "new pilot inherited another career's world");
    campaign.activate(0);
    require(campaign.load_world("morning").has_value(), "switching pilots lost the original saved world");
    campaign.deactivate();
    require(campaign.active_pilot() == nullptr && !campaign.load_world("morning"), "trainee retained named pilot state");
  }
  {
    game::campaign_store campaign{directory};
    campaign.activate(0);
    auto finish_world{[&](std::string_view const name, std::span<uint8_t const> const asset){
      auto world{world::decode_theatre(asset)};
      for(auto &object : world.objects) object.flags |= std::to_underlying(world::object_flag::objective_complete);
      game::sortie_statistics result{
        .objectives_completed{4},
      };
      result.mark(game::sortie_status::home);
      result.mark(game::sortie_status::landed);
      campaign.finish_sortie(name, world, result);
    }};
    finish_world("morning", corncob::resources::morning_asset());
    finish_world("dusk", corncob::resources::dusk_asset());
    require(campaign.active_pilot()->awards(game::medal::combat_master) == 0, "clearance awarded before the third theatre");
    finish_world("wasteland", corncob::resources::wasteland_asset());
    require(campaign.active_pilot()->awards(game::medal::combat_master) == 1 && campaign.active_pilot()->rank() == game::pilot_rank::captain,
      "clearing the required theatres did not award Captain/Combat Master clearance");
    auto const awards{campaign.active_pilot()->awards(game::medal::theatre_combat)};
    finish_world("wasteland", corncob::resources::wasteland_asset());
    require(campaign.active_pilot()->awards(game::medal::theatre_combat) == awards, "a completed theatre repeatedly awarded medals");
    auto sands{world::decode_theatre(corncob::resources::white_sands_asset())};
    auto const plane{std::ranges::find_if(sands.objects, [](auto const &object){return object.type == world::object_type::aircraft && (object.flags & 0x2000u) != 0;})};
    sands.start = corncob::maths::world_position{plane->position};
    sands.start.y += 3000;
    corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset()};
    game::session ending{geometry, std::move(sands)};
    ending.set_experimental_clearance(campaign.active_pilot()->awards(game::medal::combat_master) != 0);
    ending.advance({});
    require(ending.player() == game::player_mode::in_aircraft, "ending pilot could not enter the experimental aircraft");
    ending.relocate({
      .title{"High altitude test"},
      .position{0, 0, 1'000'000},
      .heading{0},
    });
    ending.advance({});
    require(ending.player() == game::player_mode::earth_saved && ending.finished(), "cleared experimental high-altitude flight did not reach the ending");
    campaign.finish_sortie("white_sands", ending.snapshot(), ending.results());
    require(campaign.active_pilot()->rank() == game::pilot_rank::major, "saving Earth did not award the normal game's final rank");
  }
  {
    game::campaign_store campaign{directory};
    campaign.activate(1);
    game::sortie_statistics result{
      .objectives_completed{2},
    };
    campaign.finish_sortie("morning", theatre, result);
    auto const second_score{campaign.active_pilot()->career_score()};
    auto const second_references{std::vector<uint8_t>{campaign.active_pilot()->theatres().begin(), campaign.active_pilot()->theatres().end()}};
    bool duplicate_rejected{false};
    try { campaign.rename_pilot(1, "Renamed pilot"); } catch(std::invalid_argument const &) { duplicate_rejected = true; }
    require(duplicate_rejected && campaign.active_pilot()->name() == "Other pilot", "duplicate rename modified a different career");
    campaign.delete_pilot(0);
    require(campaign.pilots().size() == 1 && campaign.active_pilot()->name() == "Other pilot", "deletion shifted active selection onto the wrong pilot");
    require(campaign.active_pilot()->career_score() == second_score && campaign.load_world("morning").has_value(), "deleting the previous slot lost the surviving pilot's world");
    require(std::ranges::equal(campaign.active_pilot()->theatres(), second_references), "deletion renumbered another pilot's world links");
    require(std::ranges::any_of(campaign.high_scores().best_careers(), [](auto const &row){return game::score_text(row.name) == "Renamed pilot" && row.retired != 0;}), "deleted career was not retained as a retired high score");
    campaign.create_pilot("Renamed pilot");
    require(!campaign.load_world("morning"), "reused pilot name inherited the deleted career's world");
    campaign.delete_pilot(1);
    require(!campaign.active_pilot(), "deleting the active pilot left a dangling selection");
  }
  {
    /// A lost aircraft in another theatre must not disqualify a clean completion, and saved theatre totals cannot inherit the whole career
    auto const isolated{directory / "award-rules"};
    game::campaign_store campaign{isolated};
    campaign.create_pilot("Award rules");
    bool living_resurrection_rejected{false};
    try { campaign.resurrect(0); } catch(std::invalid_argument const &) { living_resurrection_rejected = true; }
    require(living_resurrection_rejected, "resurrection was allowed for a living pilot");
    game::sortie_statistics loss{
      .aircraft_lost{1},
      .objectives_completed{1},
    };
    campaign.finish_sortie("dusk", world::decode_theatre(corncob::resources::dusk_asset()), loss);
    auto completed{world::decode_theatre(corncob::resources::morning_asset())};
    for(auto &object : completed.objects) object.flags |= std::to_underlying(world::object_flag::objective_complete);
    game::sortie_statistics clean{
      .objectives_completed{4},
    };
    campaign.finish_sortie("morning", completed, clean);
    require(campaign.active_pilot()->awards(game::medal::distinguished_flying_cross) == 1, "another theatre's losses disqualified a clean completion");
    auto const saved{corncob::platform::read_binary(isolated / "THT/2.THT")};
    corncob::resources::binary_reader const input{saved};
    game::pilot_record const theatre_pilot{std::span{saved}.subspan(48,623)};
    require(theatre_pilot.career().objectives_completed == 4 && theatre_pilot.career().aircraft_lost == 0, "saved theatre copied global career statistics");
    require(input.word(40) == 0 && input.word(46) == 1, "native remaining/completion header words were not saved");
    game::sortie_statistics friendly_loss{
      .objectives_completed{5},
      .friendly_destroyed{2},
    };
    campaign.finish_sortie("morning", completed, friendly_loss);
    require(!campaign.can_fly() && campaign.active_pilot()->awards(game::medal::distinguished_service_cross) == 0, "friendly losses failed to suspend the pilot and skip awards");
    require(campaign.active_pilot()->career().objectives_completed == 10, "suspension incorrectly discarded sortie statistics");
    auto settings{campaign.training_settings()};
    settings.kla.count = 3;
    settings.vehicles.quality = 7;
    settings.invincible = false;
    campaign.training_settings(settings);
    game::campaign_store reopened{isolated};
    auto const restored{reopened.training_settings()};
    require(restored.kla.count == 3 && restored.vehicles.quality == 7 && !restored.invincible, "training preferences were not restored from the native pilot record");
    require(!reopened.can_fly(), "court-martial sentence did not survive restart");
    reopened.create_pilot("Replacement");
    require(reopened.can_fly(), "another pilot inherited the suspension");
    bool activation_rejected{false};
    try { reopened.activate(0); } catch(std::invalid_argument const &) { activation_rejected = true; }
    require(activation_rejected && reopened.active_pilot()->name() == "Replacement", "suspended pilot activation replaced the current pilot");
  }
  std::filesystem::remove_all(directory);
  std::cout << "Native masked roster, career progression and independent live theatre persistence survive restart" << std::endl;
  return EXIT_SUCCESS;
}

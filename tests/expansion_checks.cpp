#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include "game/front_end.h"
#include "graphics/scene.h"
#include "resources/airfield_survey.h"
#include "resources/assets.h"
#include "world/airfields.h"
#include "world/theatre_catalog.h"

namespace {

void require(bool const condition, char const *const message) {
  if(!condition) throw std::runtime_error{message};
}

} // anonymous namespace

int main(int const argc, char const *const argv[]) {
  if(argc != 2) throw std::invalid_argument{"expansion_checks requires its dedicated scratch directory"};
  std::filesystem::path const scratch{argv[1]};
  if(scratch.filename() != "expansion-checks") throw std::invalid_argument{"unsafe expansion test directory"};
  std::filesystem::remove_all(scratch);
  namespace game = corncob::game;
  namespace world = corncob::world;
  namespace resources = corncob::resources;
  using enum game::interface_command;
  resources::collection const assets{resources::front_end_asset()};
  resources::collection const expansion{resources::other_worlds_asset(), &assets};
  resources::geometry_bank geometry{resources::engine_data_asset()};
  auto const cockpit{resources::decode_cockpit_mask(resources::cockpit_asset())};
  unsigned int worlds{0};
  for(auto const group : {world::theatre_group::earth, world::theatre_group::other_worlds, world::theatre_group::practice}) {
    for(auto const &definition : world::theatres(group)) {
      auto original{world::load_theatre(definition.name)};
      require(original.expansion == (definition.campaign == world::campaign_id::other_worlds), "world lost its edition");
      require(!original.objects.empty(), "world has no decoded scenery");
      auto const encoded{world::encode_theatre(original)};
      auto const decoded{world::decode_theatre(encoded)};
      require(decoded.objects.size() == original.objects.size(), "world did not survive native encoding");
      for(unsigned int field{0}; field != world::airfield_count; ++field) {
        auto copy{world::theatre{original}};
        world::select_airfield(copy, field);
        auto flight{std::make_unique<game::session>(geometry, std::move(copy))};
        auto renderer{std::make_unique<corncob::graphics::scene_renderer>(geometry, cockpit)};
        renderer->initialise(flight->camera().position, flight->random_stream());
        corncob::render::framebuffer frame;
        for(unsigned int step{0}; step != 3; ++step) flight->advance({});
        renderer->render(flight->world(), flight->camera(), flight->frame_number(), static_cast<uint16_t>(flight->timer()), flight->random_stream(), frame);
        auto const survey{resources::airfield_survey(original, field)};
        require(!survey.empty(), "airfield has no survey document");
        game::tower_briefing briefing{original.briefing, field};
        briefing.render(frame, assets.file("font16.bin"));
        if(!world::multiple_airfields(original)) break;
      }
      ++worlds;
    }
  }
  require(worlds == 20, "missing distributed worlds");
  auto const flatland{world::load_theatre("other_worlds/flatland")};
  auto const iceworld{world::load_theatre("other_worlds/iceworld")};
  require(flatland.environment.atmosphere_height == 1500 && flatland.environment.sea_level_density == 10000, "Flatland atmosphere is wrong");
  require(iceworld.environment.gravity == -16 && iceworld.environment.atmosphere_height == 20000, "Iceworld physics is wrong");
  require(world::multiple_airfields(flatland), "nine-airfield campaign was flattened to RDU");
  unsigned int local_objectives{0};
  for(unsigned int field{0}; field != world::airfield_count; ++field) {
    auto const start{world::airfield_start(flatland, field)};
    require(world::nearest_airfield(flatland, start) == field, "airfield IDs do not match native tower sections");
    local_objectives += resources::survey_airfield(flatland, field).objectives;
  }
  auto single{world::theatre{flatland}};
  for(auto &object : single.objects) {
    if(object.type == world::object_type::control_tower && world::airfield_for(object.position) != world::central_airfield) object.flags = 0;
  }
  require(local_objectives == resources::survey_airfield(single).objectives, "airfield partitions lose or duplicate objectives");

  game::campaign_store campaign{scratch};
  campaign.create_pilot("World Traveller");
  campaign.open_theatre("morning", world::load_theatre("morning"));
  game::sortie_statistics result{
    .objectives_completed{7},
  };
  result.mark(game::sortie_status::home);
  result.mark(game::sortie_status::landed);
  campaign.finish_sortie("morning", world::load_theatre("morning"), result);
  require(campaign.active_pilot()->rank() == game::pilot_rank::captain, "Earth progression changed");
  auto const earth_bytes{std::vector<uint8_t>{campaign.active_pilot()->native_data().begin(), campaign.active_pilot()->native_data().end()}};
  campaign.select_campaign(world::campaign_id::other_worlds);
  require(campaign.active_pilot() && campaign.active_pilot()->name() == "World Traveller", "campaign switch lost pilot identity");
  require(campaign.active_pilot()->sorties() == 0 && campaign.theatres().empty(), "Earth progress leaked into expansion");
  campaign.open_theatre("other_worlds/flatland", flatland);
  result.objectives_completed = 20;
  campaign.finish_sortie("other_worlds/flatland", flatland, result);
  require(campaign.active_pilot()->rank() == game::pilot_rank::major, "expansion Major promotion is missing");
  campaign.select_campaign(world::campaign_id::earth);
  require(std::ranges::equal(campaign.active_pilot()->native_data(), earth_bytes), "expansion mutated Earth career");
  campaign.rename_pilot(0, "Both Campaigns");
  campaign.select_campaign(world::campaign_id::other_worlds);
  require(campaign.active_pilot()->name() == "Both Campaigns" && campaign.load_world("other_worlds/flatland").has_value(), "rename lost expansion ownership");
  game::campaign_store reopened{scratch};
  reopened.select_campaign(world::campaign_id::other_worlds);
  require(reopened.pilots().size() == 1 && reopened.pilots()[0].sorties() == 1, "expansion career did not survive restart");

  auto front{std::make_unique<game::front_end>(assets, reopened, true)};
  front->character('s');
  require(front->current_page() == game::interface_page::campaigns, "combined campaign selector is unreachable");
  front->character('o');
  require(front->current_page() == game::interface_page::new_theatre, "expansion list is unreachable");
  front->command(end);
  front->command(accept);
  require(reopened.theatres().size() == 2, "last expansion world could not be selected");
  front->character('f');
  front->command(accept);
  auto launch{front->take_launch()};
  if(!launch) {
    front->command(accept);
    launch = front->take_launch();
  }
  require(launch && launch->theatre == "other_worlds/whtsands", "expansion menu launched the wrong world");
  // The builder changes only the opened copy, and its cursor never leaks into a native saved universe.
  game::mission_builder builder{single, geometry};
  unsigned int const count_before{static_cast<unsigned int>(std::ranges::count_if(single.objects, [](auto const &object){return object.has(world::object_flag::present);} ))};
  builder.command(game::builder_command::cursor);
  builder.command(game::builder_command::create);
  auto const selected{builder.selected_object()};
  require(selected.has_value(), "builder did not select its newly created object");
  builder.command(game::builder_command::next_quality);
  builder.command(game::builder_command::special);
  builder.command(game::builder_command::complete);
  require(single.objects[*selected].quality() == 1 && single.objects[*selected].has(world::object_flag::special)
    && single.objects[*selected].has(world::object_flag::objective_complete), "builder did not apply native quality/special/completion fields");
  builder.command(game::builder_command::next_type);
  require(single.objects[*selected].type != world::object_type::generator_orb, "builder did not cycle object definitions");
  auto const edited{world::decode_theatre(world::encode_theatre(single))};
  require(edited.objects.size() == count_before + 1, "editor cursor leaked into an exported world");
  builder.command(game::builder_command::erase);
  require(world::decode_theatre(world::encode_theatre(single)).objects.size() == count_before, "builder deletion did not persist");
  auto editor_session{std::make_unique<game::session>(geometry, world::load_theatre("other_worlds/default"))};
  editor_session->begin_builder();
  unsigned int rendered{0};
  auto draw_editor{[&]{++rendered;}};
  editor_session->advance({}, std::function_ref<void()>{draw_editor});
  require(rendered == 1, "builder failed to refresh the native flight frame");
  corncob::graphics::title_sequence expansion_title{expansion, true};
  corncob::render::framebuffer title_frame;
  for(unsigned int tick{0}; tick != 450; ++tick) expansion_title.advance(.05);
  expansion_title.render(title_frame);
  require(title_frame.width == 320 && title_frame.height == 200, "expansion title has invalid dimensions");
  // A full required-world completion grants General and shows the expansion's original celebration.
  game::campaign_store victory{scratch / "victory"};
  victory.create_pilot("Campaign Master");
  victory.select_campaign(world::campaign_id::other_worlds);
  game::pilot_record prior;
  for(auto const &definition : world::other_worlds_catalog) {
    if(!definition.required) continue;
    auto completed{world::load_theatre(definition.name)};
    for(auto &object : completed.objects) object.flags |= std::to_underlying(world::object_flag::objective_complete);
    victory.open_theatre(definition.name, completed);
    prior = *victory.active_pilot();
    victory.finish_sortie(definition.name, completed, result);
  }
  require(victory.active_pilot()->rank() == game::pilot_rank::general, "required expansion campaign did not grant General");
  auto celebration{std::make_unique<game::front_end>(assets, victory, true)};
  celebration->finish_sortie(result, &prior);
  for(unsigned int page{0}; page != 40 && celebration->current_page() != game::interface_page::campaign_victory; ++page) celebration->command(accept);
  require(celebration->current_page() == game::interface_page::campaign_victory, "Master of Corncob celebration was omitted");
  celebration->render(title_frame);
  celebration->command(accept);
  require(celebration->current_page() == game::interface_page::menu, "campaign completion did not return to the unified menu");
  // Deleting the shared identity must retire both careers and their separately owned universes.
  reopened.delete_pilot(0);
  reopened.select_campaign(world::campaign_id::earth);
  require(reopened.pilots().empty() && reopened.theatres().empty(), "pilot deletion orphaned an Earth career");
  reopened.select_campaign(world::campaign_id::other_worlds);
  require(reopened.pilots().empty() && reopened.theatres().empty(), "pilot deletion orphaned an expansion career");
  std::cout << worlds << " worlds rendered; native-format campaign isolation, airfields, physics and menu launches pass\n";
}

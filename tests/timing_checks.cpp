#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include "game/session.h"
#include "game/timing.h"
#include "resources/assets.h"
#include "world/motion.h"
#include "world/training.h"

namespace {
void require(bool value, char const *message) { if(!value) throw std::runtime_error{message}; }
using namespace corncob;
using cadence = std::array<double,4>;
std::array<cadence,5> const cadences{{
  {1.0/30,1.0/30,1.0/30,1.0/30}, {1.0/60,1.0/60,1.0/60,1.0/60},
  {1.0/144,1.0/144,1.0/144,1.0/144}, {0.005,0.027,0.011,0.042}, {0.001,0.001,0.001,0.001}
}};
template<class Function> void run(double seconds, cadence const &frames, Function update) {
  unsigned int index{0}; double now{0};
  while(seconds-now > 1e-10) {
    double const dt{std::min(frames[index%frames.size()],seconds-now)};
    now += dt; update(dt,now,index++);
  }
}
void clock_checks() {
  for(auto const &frames : cadences) {
    game::timing::frame_clock clock;
    require(clock.advance(100)==0,"first callback advanced time before establishing a timestamp");
    double total{0};
    run(3,frames,[&](double,double now,unsigned int){ total += clock.advance(100+now); });
    require(std::abs(total-3)<1e-9,"irregular host scheduling changed elapsed game time");
    require(clock.advance(500,false)==0 && clock.advance(1000)==0,"resuming a suspended callback caught up hidden-tab time");
    require(std::abs(clock.advance(1001)-0.1)<1e-10,"a long foreground stall exceeded the catch-up bound");
    clock.reset(); clock.advance(0);
    require(std::abs(clock.advance(0.05,true,true)-0.5)<1e-10,"T did not scale elapsed time before subdivision");
  }
}
void pedestrian_checks(resources::geometry_bank &geometry) {
  for(auto const &frames : cadences) {
    world::theatre ground; ground.start={0,3000,720};
    game::session walk{geometry,ground};
    run(4,frames,[&](double dt,double,unsigned int i){walk.advance_time({.walk_forward{true},.look_yaw{static_cast<int16_t>(i==0 ? 8000 : 0)}},dt);});
    auto const distance{static_cast<int32_t>(4*99/game::timing::seconds_per_step)};
    require(std::abs(walk.pilot().position.x-distance)<=1 && walk.pilot().position.y==0,"walking speed changed with frame rate or lost fractional movement");
    require(walk.timer()==4660 && walk.frame_number()==116,"game clocks followed rendered frames instead of elapsed time");
    game::session jump{geometry,ground};
    run(1,frames,[&](double dt,double,unsigned int){jump.advance_time({.jump{true}},dt);});
    require(std::abs(jump.pilot().position.z-5802)<=2 && std::abs(jump.pilot().velocity.z-1699)<=1,"jump trajectory changed with frame rate");
    game::session mouse{geometry,ground};
    mouse.advance_time({.look_yaw{1200}},0.1);
    require(mouse.pilot().attitude.heading==static_cast<maths::angle>(-8000+1200),"bounded substeps applied one mouse delta repeatedly");
    game::session browser{geometry,ground};
    browser.update_frame(0,{});
    auto const initial_browser{maths::world_position{browser.pilot().position}};
    browser.update_frame(1.0/144,{.walk_forward{true}});
    require(browser.pilot().position != initial_browser && browser.frame_number()==0,"a display callback waited for a legacy tick before simulating movement");
    auto const before{maths::world_position{browser.pilot().position}};
    browser.suspend_clock(); browser.update_frame(10000,{.walk_forward{true}});
    require(browser.pilot().position==before,"returning from a menu or hidden tab moved the pilot");
    browser.press(game::action::pause); browser.press(game::action::pause);
    browser.update_frame(20000,{.walk_forward{true}});
    require(browser.pilot().position==before,"pausing without host callbacks caught up the paused interval");
    game::session fast{geometry,ground}; fast.update_frame(0,{});
    run(1,frames,[&](double,double now,unsigned int){fast.update_frame(now,{.walk_forward{true}},true);});
    require(fast.timer()==11652 && fast.frame_number()==291,"fast-forward changed timer/animation rates across callback cadences");
  }
}
void flight_checks(resources::geometry_bank &geometry) {
  for(bool beginner : {false,true}) {
    std::optional<flight::aircraft_state> baseline;
    for(unsigned int n{0}; n<4; ++n) {
      world::theatre theatre; theatre.training=true;
      game::session sortie{geometry,theatre};
      sortie.relocate({.title{"Timing check"},.position{0,0,200000},.heading{0}});
      sortie.set_invincible(true); sortie.set_beginner_flight(beginner);
      run(5,cadences[n],[&](double dt,double,unsigned int){sortie.advance_time({.stick{.roll{64},.pitch{-64}},.throttle_up{true},.weapons{.gun{true}}},dt);});
      require(sortie.player()==game::player_mode::in_aircraft,"elapsed flight unexpectedly killed or ejected the pilot");
      require(sortie.sounds().gunshots==39,"weapon cadence followed rendered frames");
      if(!baseline) baseline=sortie.craft();
      for(unsigned int axis{0}; axis<3; ++axis) {
        require(std::abs(sortie.craft().position[axis]-baseline->position[axis])<250,"flight trajectory depended materially on callback frequency");
        require(std::abs(sortie.craft().velocity[axis]-baseline->velocity[axis])<30,"flight velocity depended materially on callback frequency");
      }
      require(std::abs(maths::word(sortie.craft().attitude.heading-baseline->attitude.heading))<100
        && std::abs(maths::word(sortie.craft().attitude.pitch-baseline->attitude.pitch))<100
        && std::abs(maths::word(sortie.craft().attitude.roll-baseline->attitude.roll))<100,"flight rotation depended materially on callback frequency");
    }
  }
}
void interaction_and_animation_checks(resources::geometry_bank &geometry) {
  for(unsigned int n{0}; n<3; ++n) {
    auto theatre{world::generate_training(geometry,{.kla{0},.vehicles{0},.aaa{0}})};
    game::session takeoff{geometry,std::move(theatre)};
    takeoff.set_invincible(true);
    run(20,cadences[n],[&](double dt,double now,unsigned int){
      int const pitch{now>8.5 ? std::clamp((maths::word(takeoff.craft().attitude.pitch)+2500)/8,-256,256) : 0};
      takeoff.advance_time({.stick{.pitch{static_cast<int16_t>(pitch)}},.throttle_up{now<0.5}},dt);
    });
    require(takeoff.player()==game::player_mode::in_aircraft && takeoff.craft().position.z>5000,"elapsed-time takeoff did not produce sustained flight");
    world::theatre ground; ground.training=true; ground.start.z=720;
    game::session reentry{geometry,ground}; reentry.press(game::action::eject);
    run(2,cadences[n],[&](double dt,double,unsigned int){reentry.advance_time({.walk_backward{true}},dt);});
    require(reentry.player()==game::player_mode::on_foot,"walking away from the aircraft immediately re-entered it");
    run(3,cadences[n],[&](double dt,double,unsigned int){reentry.advance_time({.walk_forward{true}},dt);});
    require(reentry.player()==game::player_mode::in_aircraft,"elapsed-time walking failed to re-enter an aircraft");
  }
  for(auto const &frames : cadences) {
    world::theatre theatre;
    theatre.objects.push_back({.position{0,0,200000},.attitude{},.type{world::object_type::saucer},.flags{0x19},.timer{32000},.health{50}});
    game::combat weapons{geometry,theatre}; game::actors actors{geometry,theatre,weapons};
    maths::world_position const target{0,0,720}; maths::fixed_vector const velocity{0,0,0};
    run(1,frames,[&](double dt,double,unsigned int){actors.advance({target,target,velocity,false,0},{.seconds{dt},.clock_tick{false}});});
    auto const expected{static_cast<maths::angle>(static_cast<int>(4000/game::timing::seconds_per_step))};
    require(std::abs(maths::word(theatre.objects[0].attitude.heading-expected))<=1 && theatre.objects[0].timer==32000,"saucer rotation waited for AI ticks or changed with callback cadence");
    std::vector<uint8_t> recording(37*3);
    auto put{[&](unsigned int offset,uint32_t value){for(unsigned int b{0};b<4;++b) recording[offset+b]=static_cast<uint8_t>(value>>(b*8));}};
    recording[0]=recording[37]=11; recording[74]=255;
    put(1,10000); put(9,200000); put(38,10200); put(46,200000);
    game::session demo{geometry,{}}; demo.begin_recording(recording);
    require(demo.craft().position.x==10000,"recorded demonstration did not establish its initial pose");
    run(0.25,frames,[&](double dt,double,unsigned int){demo.advance_time({.weapons{.gun{true}}},dt);});
    require(demo.finished() && demo.sounds().gunshots==0,"recorded playback duration or input isolation changed with callback cadence");
  }
}

void assistance_checks(resources::geometry_bank &geometry) {
  std::optional<flight::aircraft_state> baseline;
  std::optional<maths::world_position> capture_baseline;
  for(unsigned int n{0}; n<4; ++n) {
    world::theatre theatre; theatre.training=true;
    game::session sortie{geometry,theatre};
    sortie.relocate({.title{"Autopilot timing"},.position{0,0,200000},.heading{0}});
    sortie.set_invincible(true); sortie.press(game::action::toggle_autopilot);
    run(10,cadences[n],[&](double dt,double,unsigned int){sortie.advance_time({},dt);});
    require(sortie.autopilot_engaged(),"elapsed flight unexpectedly disengaged autopilot");
    if(!baseline) baseline=sortie.craft();
    for(unsigned int axis{0}; axis<3; ++axis)
      require(std::abs(sortie.craft().position[axis]-baseline->position[axis])<500,"autopilot trajectory depended materially on callback frequency");
    world::theatre ground; ground.start={0,3000,720};
    ground.objects.push_back({.position{6000,0,0},.attitude{},.type{world::object_type::ground_vehicle},.flags{0x15},.timer{32000}});
    game::session capture{geometry,ground};
    run(2,cadences[n],[&](double dt,double,unsigned int){capture.advance_time({},dt);});
    require(capture.player()==game::player_mode::on_foot,"capture timing fixture finished before measuring its approach");
    auto const distance{(vec3<double>{capture.pilot().position}-vec3<double>{0,0,720}).length()};
    require(distance>1700 && distance<1900,"capture approach no longer uses its intended walking rate");
    if(!capture_baseline) capture_baseline=capture.pilot().position;
    for(unsigned int axis{0}; axis<3; ++axis)
      require(std::abs(capture.pilot().position[axis]-(*capture_baseline)[axis])<50,"capture approach speed depended on callback frequency");
  }
}

void damage_impulse_checks() {
  std::optional<vec3<double>> baseline;
  for(double dt : {1.0/60,1.0/144,0.001}) {
    flight::aircraft_state a; a.velocity={5000,0,0}; a.body_velocity={5000,0,0}; a.scaled_body_velocity={300,0,0};
    auto b{a}; flight::angular_response clean, damaged; clean.stability_damage=damaged.stability_damage=120;
    flight::continuous_state plain, jitter; maths::random_source random;
    flight::controls_elapsed(a,clean,plain,{}, {},dt,nullptr);
    flight::controls_elapsed(b,damaged,jitter,{}, {},dt,&random);
    double const ratio{dt/game::timing::seconds_per_step};
    vec3<double> impulse{(damaged.roll+jitter.response.x-clean.roll-plain.response.x)/maths::decay(0.8125,ratio),
                         (damaged.pitch+jitter.response.y-clean.pitch-plain.response.y)/maths::decay(0.9,ratio),
                         (damaged.yaw+jitter.response.z-clean.yaw-plain.response.z)/maths::decay(0.9,ratio)};
    if(!baseline) baseline=impulse;
    for(unsigned int axis{0};axis<3;++axis) require(std::abs(impulse[axis]-(*baseline)[axis])<2,"damage jitter weakened at higher simulation frequencies");
  }
}

void contact_checks(resources::geometry_bank &geometry) {
  for(auto const &frames : cadences) {
    world::theatre theatre;
    theatre.objects.push_back({.position{0,0,10000},.attitude{},.type{world::object_type::aircraft},.flags{9}});
    theatre.objects.push_back({.position{0,0,10000},.attitude{},.type{world::object_type::explosion},.flags{1}});
    game::combat weapons{geometry,theatre};
    theatre.objects[0].health=100;
    theatre.objects[0].collision_group=1; theatre.objects[1].collision_group=2;
    theatre.objects[0].collision_extents=theatre.objects[1].collision_extents=maths::fixed_vector{1000,1000,1000};
    maths::world_position const observer{1000000,1000000,720};
    double phase{0};
    run(1,frames,[&](double dt,double,unsigned int){
      while(dt>1e-12) {
        double const duration{std::min(dt,game::timing::maximum_step)};
        phase+=duration;
        bool const tick{phase+1e-12>=game::timing::seconds_per_step};
        if(tick) phase-=game::timing::seconds_per_step;
        weapons.advance(observer,{.seconds{duration},.clock_tick{tick}});
        dt-=duration;
      }
    });
    require(theatre.objects[0].health==70,"persistent collision damage increased with callback frequency");
    theatre.objects[1].position.x=20000;
    weapons.advance(observer,{.seconds{0.001},.clock_tick{false}});
    theatre.objects[1].position.x=0;
    weapons.advance(observer,{.seconds{0.001},.clock_tick{false}});
    require(theatre.objects[0].health==69,"a new contact waited for the next legacy damage tick");
  }
}

void production_and_motion_checks(resources::geometry_bank &geometry) {
  std::optional<world::placed_object> baseline;
  for(auto const &frames : cadences) {
    world::theatre theatre;
    theatre.objects.push_back({.position{4000000,4000000,0},.attitude{},.type{world::object_type::kla_site},.flags{geometry.definition(world::object_type::kla_site).flags},.timer{300}});
    game::session sites{geometry,theatre};
    run(8,frames,[&](double dt,double,unsigned int){sites.advance_time({},dt);});
    unsigned int children{0};
    for(auto const &object : sites.world().objects) if(object.type==world::object_type::kla && object.has(world::object_flag::present)) {
      ++children; require(object.timer==23968,"enemy production or lifetimes followed rendered frames");
    }
    require(children==1,"elapsed-time production violated the local population cap");
    world::placed_object object{.position{0,0,200000},.attitude{},.velocity{1000,100,1000},.motion{.gravity{20}}};
    maths::random_source random;
    run(5,frames,[&](double dt,double,unsigned int){world::advance_motion(object,true,random,{.seconds{dt},.clock_tick{false}});});
    if(!baseline) baseline=object;
    for(unsigned int axis{0}; axis<3; ++axis) require(std::abs(object.position[axis]-baseline->position[axis])<=1 && std::abs(object.velocity[axis]-baseline->velocity[axis])<=1,"moving objects lost fractional gravity or displacement");
  }
}
}
int main() {
  corncob::resources::geometry_bank geometry{corncob::resources::engine_data_asset()};
  clock_checks(); pedestrian_checks(geometry); flight_checks(geometry); production_and_motion_checks(geometry); interaction_and_animation_checks(geometry); contact_checks(geometry); damage_impulse_checks(); assistance_checks(geometry);
  std::cout << "Elapsed input, walking, jumping, flight, weapons, production and motion pass at 30/60/144/1000 FPS and irregular callbacks\n";
}

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "audio/effects.h"
#include "audio/music.h"
#include "resources/assets.h"

namespace {
void require(bool const value,char const *message) { if(!value) throw std::runtime_error{message}; }
}
int main() {
  namespace audio = corncob::audio;
  std::vector<float> full(96000), half(96000), muted(96000);
  audio::music normal, quiet, silent;
  quiet.volume(50); silent.volume(0);
  for(auto *player : {&normal,&quiet,&silent}) player->select(audio::music_cue::title);
  std::ranges::fill(half,0.125f); std::ranges::fill(muted,0.125f);
  normal.mix(full); quiet.mix(half); silent.mix(muted);
  float peak{0};
  for(unsigned int i{0};i<full.size();++i) {
    peak=std::max(peak,std::abs(full[i]));
    require(std::abs(half[i]-std::clamp(0.125f+full[i]*0.5f,-1.0f,1.0f))<1e-7f,"music gain changed the existing effects mix or used the wrong level");
    require(muted[i]==0.125f,"zero music volume leaked audio or muted sound effects");
  }
  require(peak>0.001f,"music volume check used silent material");
  silent.volume(100);
  std::ranges::fill(full,0);std::ranges::fill(muted,0);
  normal.mix(full);silent.mix(muted);
  for(unsigned int i{512};i<full.size();++i)
    require(full[i]==muted[i],"unmuting music restarted or desynchronised its playback");
  corncob::resources::collection resources{corncob::resources::front_end_asset()};
  audio::effects engine{resources}, soft{resources}, off{resources};
  soft.volume(50);off.volume(0);
  audio::frame_state const frame{.tick{10000},.oil_pressure{25000},.forward_velocity{5000},.active{true},.engine{true}};
  for(auto *effects : {&engine,&soft,&off}) effects->update(frame);
  engine.render(full);soft.render(half);off.render(muted);peak=0;
  for(unsigned int i{0};i<full.size();++i) {
    peak=std::max(peak,std::abs(full[i]));
    require(half[i]==full[i]*0.5f,"effects volume did not scale the live synthesizer output");
    require(muted[i]==0,"zero effects volume leaked sound");
  }
  require(peak>0.001f,"effects volume check used silent material");
  engine.volume(0);engine.render(full);
  require(std::ranges::all_of(std::span{full}.subspan(512),[](float x){return x==0;}),"effects volume ramp did not settle to silence");
  std::cout << "Independent music/effects gains, silent startup, preserved mixes and click-free live volume changes pass\n";
}

#include "sound/rain.h"
#include "sound/engine.h"
#include "main.h"
#include "physics/rain.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
void internal_error(const std::string& message,std::source_location) { std::cerr<<message; std::abort(); }
void require(bool value,const char* label) { if (!value) { std::cerr<<label<<'\n'; std::exit(1); } }
double energy(const short* data,int length) {
    double sum=0; for (int i=0;i<length;++i) sum+=double(data[i])*data[i]; return sum;
}
int main() {
    std::array<short,2205> buffer{};
    rain_audio::reset(); rain_audio::impact({0,0},0,{0,0});
    rain_audio::mix(buffer.data(),buffer.size(),true);
    double near=energy(buffer.data(),buffer.size());
    int peak=0;
    for (short sample:buffer) peak=std::max(peak,std::abs(int(sample)));
    require(peak>4000,"nearby rain has an audible signal level");
    require(near>0 && energy(buffer.data()+700,1505)==0,"short rain patter ends cleanly");
    buffer={}; rain_audio::reset(); rain_audio::impact({15,0},0,{0,0});
    rain_audio::mix(buffer.data(),buffer.size(),true);
    require(energy(buffer.data(),buffer.size())<near/4,"distant impacts quieter");
    buffer={}; rain_audio::reset(); rain_audio::impact({36,0},0,{0,0});
    rain_audio::mix(buffer.data(),buffer.size(),true);
    require(energy(buffer.data(),buffer.size())==0,"far impacts culled");
    buffer={}; rain_audio::reset();
    rain_audio::impact({0,0},0,{0,0}); rain_audio::impact({0,0},0.11,{0,0});
    rain_audio::mix(buffer.data(),buffer.size(),true);
    require(energy(buffer.data(),660)>0 && energy(buffer.data()+700,400)==0 &&
            energy(buffer.data()+1220,650)>0,"impact spacing retained within an audio buffer");
    buffer={}; rain_audio::reset();
    for (int i=0;i<10000;++i) rain_audio::impact({0,0},0,{0,0});
    rain_audio::mix(buffer.data(),buffer.size(),true);
    require(energy(buffer.data(),buffer.size())<near*2,"simultaneous storm is capped");
    buffer={}; rain_audio::reset(); rain_audio::impact({0,0},0,{0,0});
    rain_audio::mix(buffer.data(),buffer.size(),false);
    rain_audio::mix(buffer.data(),buffer.size(),true);
    require(energy(buffer.data(),buffer.size())==0,"mute discards queued sounds");
    rain_audio::impact({0,0},1,{0,0}); rain_audio::reset();
    rain_audio::mix(buffer.data(),buffer.size(),true);
    require(energy(buffer.data(),buffer.size())==0,"level reset discards previous impacts");
    buffer={}; rain_audio::reset();
    rain_audio::impact({24,0},0,{0,0}); rain_audio::impact({0,0},0.001,{0,0});
    rain_audio::mix(buffer.data(),buffer.size(),true);
    require(energy(buffer.data(),buffer.size())>near*0.5,"distant impacts cannot suppress nearby rain");
    buffer={}; rain_audio::reset();
    rain::settings().frequency=0;
    rain::set_scene({{{-10,-10},{10,-10},{10,10},{-10,10}}});
    rain::set_impact_listener([](vect2 point,double seconds,bool water) {
        rain_audio::impact(point,seconds,{0,-9},water);
    });
    rain::add_drop({0,0});
    for (int i=0;i<600;++i) rain::update(0.005);
    rain_audio::mix(buffer.data(),buffer.size(),true);
    require(energy(buffer.data(),buffer.size())>near*0.2,"falling particle terrain impact reaches audio mixer");
    static bool hit_water=false;
    rain::set_impact_listener([](vect2,double,bool water) { hit_water=water; });
    rain::add_drop({0,-9});
    for (int i=0;i<200;++i) rain::update(0.005);
    require(hit_water,"existing puddle absorption classified as water impact");
    rain::set_impact_listener(nullptr);
    buffer={}; rain_audio::reset(); rain_audio::impact({0,0},0,{0,0});
    rain_audio::mix(buffer.data(),buffer.size(),true);
    auto dirt=buffer;
    buffer={}; rain_audio::reset(); rain_audio::impact({0,0},0,{0,0},true);
    rain_audio::mix(buffer.data(),buffer.size(),true);
    require(energy(buffer.data(),buffer.size())>near*0.2 && buffer!=dirt,"water plop is audible and distinct from dirt patter");
    std::cout<<"PASS rain patter, distance, timing, storm cap, mute and reset\n";
}

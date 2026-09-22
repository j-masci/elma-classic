#include "sound/rain.h"
#include "sound/engine.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace rain_audio {
namespace {
constexpr unsigned QUEUE_SIZE=256, VOICES=8;
constexpr int SAMPLE_LENGTH=SOUND_SAMPLE_RATE*60/1000;
struct Event { double time, gain; unsigned variant, epoch; };
std::array<Event,QUEUE_SIZE> queue;
std::atomic<unsigned> read_index{0},write_index{0},epoch{0};
struct Voice { int index=0; int64_t start=0; double gain=0; unsigned variant=0; };
std::array<Voice,VOICES> voices;
unsigned audio_epoch=0, random_state=7819;
int64_t audio_clock=0, anchor_clock=0;
double anchor_time=0, next_allowed=-1, last_gain=0;
bool anchored=false;
double random_unit() {
    random_state=random_state*1664525u+1013904223u;
    return double(random_state)/4294967296.0;
}
const auto& samples() {
    static const auto bank=[] {
        std::array<std::array<short,SAMPLE_LENGTH>,8> result{};
        uint32_t noise=1597;
        for (unsigned v=0;v<8;++v) {
            const bool water=v>=4;
            const unsigned variation=v%4;
            // A small, dull splash: filtered noise instead of a pitched tone.
            // Two smoothing bands give a soft body and a little dirt-like grain.
            double soft=0,body=0,peak=0,phase=0;
            std::array<double,SAMPLE_LENGTH> shaped{};
            for (int i=0;i<SAMPLE_LENGTH;++i) {
                double t=double(i)/SOUND_SAMPLE_RATE;
                noise=noise*1664525u+1013904223u;
                double hiss=2.0*double(noise)/4294967296.0-1.0;
                soft+=(0.18+variation*0.035)*(hiss-soft);
                body+=0.07*(soft-body);
                double envelope=(1-std::exp(-450*t))*std::exp(-(65.0+variation*5.0)*t);
                double fade=std::min(1.0,double(SAMPLE_LENGTH-1-i)/(SOUND_SAMPLE_RATE*0.006));
                double texture=0.8*soft+0.6*body+0.06*hiss;
                if (water) {
                    // A low, rounded liquid plop, distinct from the unpitched
                    // dirt patter and well below the old piercing rain tone.
                    phase+=6.283185307179586*(520+variation*65)*std::exp(-14*t)/SOUND_SAMPLE_RATE;
                    texture=0.75*std::sin(phase)+0.25*soft;
                }
                shaped[i]=envelope*fade*texture;
                peak=std::max(peak,std::abs(shaped[i]));
            }
            // Keep variants audible and similarly loud without restoring the
            // piercing sine-wave peak of the previous plink.
            for (int i=0;i<SAMPLE_LENGTH;++i)
                result[v][i]=(short)((water?6200:7000)*shaped[i]/std::max(peak,1e-12));
        }
        return result;
    }();
    return bank;
}
}
void reset() {
    next_allowed=-1;
    last_gain=0;
    epoch.fetch_add(1,std::memory_order_release);
}
void impact(vect2 position,double seconds,vect2 listener,bool water) {
    double distance=(position-listener).length();
    if (!std::isfinite(seconds) || !std::isfinite(distance) || distance>=35) return;
    double gain=(1-distance/35)/(1+distance*distance/144);
    // Off-screen drops must not consume the entire sound budget before a
    // clearly louder impact beside the rider is visited in the particle pool.
    if (seconds<next_allowed && gain<=last_gain*1.5) return;
    unsigned write=write_index.load(std::memory_order_relaxed);
    unsigned next=(write+1)%QUEUE_SIZE;
    if (next==read_index.load(std::memory_order_acquire)) return;
    queue[write]={seconds,gain*(0.8+0.2*random_unit()),unsigned(random_unit()*4)+(water?4u:0u),epoch.load(std::memory_order_acquire)};
    write_index.store(next,std::memory_order_release);
    // Real collisions trigger sounds. A randomized cooldown caps the density
    // without quantizing events to rendered frames or a regular metronome.
    next_allowed=seconds+0.012+0.023*random_unit();
    last_gain=gain;
}
void mix(short* buffer,int length,bool audible) {
    unsigned current_epoch=epoch.load(std::memory_order_acquire);
    if (audio_epoch!=current_epoch || !audible) {
        voices={}; anchored=false; audio_epoch=current_epoch;
    }
    unsigned read=read_index.load(std::memory_order_relaxed);
    unsigned write=write_index.load(std::memory_order_acquire);
    while (read!=write) {
        Event event=queue[read];
        read=(read+1)%QUEUE_SIZE;
        read_index.store(read,std::memory_order_release);
        if (!audible || event.epoch!=audio_epoch) continue;
        if (!anchored) { anchor_time=event.time; anchor_clock=audio_clock; anchored=true; }
        int64_t start=anchor_clock+(int64_t)((event.time-anchor_time)*SOUND_SAMPLE_RATE);
        // After a pause/stall, re-anchor rather than playing a delayed backlog.
        if (start<audio_clock-SOUND_SAMPLE_RATE/5 || start>audio_clock+SOUND_SAMPLE_RATE/5) {
            anchor_time=event.time; anchor_clock=audio_clock; start=audio_clock;
        }
        for (auto& voice:voices) {
            if (voice.gain==0) { voice={0,std::max(start,audio_clock),event.gain,event.variant}; break; }
        }
    }
    if (audible) {
        const auto& bank=samples();
        for (auto& voice:voices) {
            if (voice.gain==0) continue;
            for (int i=0;i<length;++i) {
                if (audio_clock+i<voice.start) continue;
                int mixed=buffer[i]+int(bank[voice.variant][voice.index++]*voice.gain);
                buffer[i]=(short)std::clamp(mixed,-32768,32767);
                if (voice.index==SAMPLE_LENGTH) { voice.gain=0; break; }
            }
        }
    }
    audio_clock+=length;
    if (std::none_of(voices.begin(),voices.end(),[](const Voice& v){return v.gain!=0;})) anchored=false;
}
}

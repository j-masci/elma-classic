#ifndef SOUND_RAIN_H
#define SOUND_RAIN_H
#include "vect2.h"
namespace rain_audio {
// Producer: game thread. Consumer: audio callback. No files or allocations.
void reset();
void impact(vect2 position, double seconds, vect2 listener, bool water = false);
void mix(short* buffer, int length, bool audible);
}
#endif

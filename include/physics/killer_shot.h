#ifndef PHYSICS_KILLER_SHOT_H
#define PHYSICS_KILLER_SHOT_H

#include "physics/init.h"
#include <array>
#include <cstddef>

namespace killer_shot {
inline constexpr double RADIUS = 0.12;
inline constexpr std::size_t MAX_SHOTS = 128;
struct Settings {
    double speed = 24.0;
    double delay_seconds = 0.25;
};
struct Shot {
    bool active = false;
    bool stopped = false;
    vect2 position;
    vect2 previous;
    vect2 velocity;
    double age_seconds = 0.0;
    double impact_seconds = 0.0;
    double moving_fraction = 1.0;
};
Settings& settings();
const std::array<Shot, MAX_SHOTS>& shots();
void reset(); // Clear shots/cooldown; keep the session's settings and aim.
double aim_degrees();
void rotate_aim(double degrees);
vect2 aim_direction(const motorst& motor);
vect2 handlebar_position(const motorst& motor);
vect2 muzzle_position(const motorst& motor);
bool spawn(const motorst& motor);
void update(double dt);
bool hits_rider(const motorst& current, const motorst& previous);
bool flash_white(const Shot& shot); // Cosmetic only: still deadly while flashing.

bool sweep_circle_segment(vect2 start, vect2 delta, double radius, vect2 a, vect2 b,
                          double& fraction);
} // namespace killer_shot
#endif

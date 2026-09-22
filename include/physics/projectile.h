#ifndef PHYSICS_PROJECTILE_H
#define PHYSICS_PROJECTILE_H

#include "physics/init.h"
#include <array>
#include <cstddef>

// Axis-aligned, infinite-mass squares for the K-key experiment. Positions
// are in physics coordinates. Previous position is kept for swept contacts.
namespace projectile {
inline constexpr double HALF_SIZE = 0.5;
inline constexpr std::size_t MAX_PROJECTILES = 64;
struct Settings {
    double speed = 6.2;
    double gravity = 10.0;
    double elevation_degrees = 15.0;
    double delay_seconds = 0.4;
    bool collide_with_squares = true;
};
Settings& settings(); // Session-only menu controls; reset() only clears shots.
struct State {
    bool active = false;
    bool stopped = false;
    vect2 position;
    vect2 previous;
    vect2 velocity;
    double age = 0.0;
};
const std::array<State, MAX_PROJECTILES>& states();
void reset();
bool spawn(const motorst& motor);
void update(double dt);
void collide_wheel(rigidbody& wheel, vect2 previous_position, double dt);
bool hits_head(vect2 position, vect2 previous_position, double radius);

// Pure geometry helpers shared with the focused prototype tests.
bool sweep_segment(vect2 center, vect2 delta, double half_size, vect2 a, vect2 b,
                   double& fraction);
bool circle_contact(vect2 relative, double radius, vect2& normal, double& depth);
} // namespace projectile
#endif

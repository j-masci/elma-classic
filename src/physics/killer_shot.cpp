#include "physics/killer_shot.h"
#include "editor/editor.h"
#include "level/level.h"
#include "level/segments.h"
#include "main.h"
#include <algorithm>
#include <cmath>

namespace killer_shot {
namespace {
std::array<Shot, MAX_SHOTS> pool;
Settings config;
double cooldown_seconds = 0.0;
double elevation = 10.0;
constexpr double PHYSICS_PER_SECOND = 1000.0 * STOPWATCH_MULTIPLIER * STOPWATCH_TO_PHYS_TIME;
constexpr double GRAVITY = 6.0;
constexpr double SKIN = 0.0001;

// Segment-versus-circle time of impact, also used for moving rider circles in
// relative coordinates. Starting overlaps are hits; a zero-length path is safe.
bool sweep_point(vect2 start, vect2 delta, vect2 center, double radius, double& fraction) {
    vect2 offset = start - center;
    double c = offset * offset - radius * radius;
    if (c <= 0.0) {
        fraction = 0.0;
        return true;
    }
    double a = delta * delta;
    if (a < 1e-18) {
        return false;
    }
    double b = offset * delta;
    double discriminant = b * b - a * c;
    if (discriminant < 0.0) {
        return false;
    }
    double t = (-b - std::sqrt(discriminant)) / a;
    if (t < 0.0 || t > 1.0) {
        return false;
    }
    fraction = t;
    return true;
}

bool hits_circle(const Shot& shot, vect2 current, vect2 previous, double radius) {
    // Split at terrain impact: the bullet moves only until moving_fraction,
    // then stays put for the rest of the step while the rider may keep moving.
    vect2 rider_at_impact = previous + (current - previous) * shot.moving_fraction;
    double fraction;
    return sweep_point(shot.previous - previous,
                       (shot.position - shot.previous) - (rider_at_impact - previous),
                       vect2(), radius + RADIUS, fraction) ||
           sweep_point(shot.position - rider_at_impact, rider_at_impact - current,
                       vect2(), radius + RADIUS, fraction);
}
} // namespace

Settings& settings() { return config; }
const std::array<Shot, MAX_SHOTS>& shots() { return pool; }
void reset() {
    pool = {};
    cooldown_seconds = 0.0;
}
double aim_degrees() { return elevation; }
void rotate_aim(double degrees) {
    if (std::isfinite(degrees)) {
        elevation = std::remainder(elevation + degrees, 360.0);
    }
}

vect2 aim_direction(const motorst& motor) {
    vect2 axis(std::cos(motor.bike.rotation), std::sin(motor.bike.rotation));
    double angle = elevation * std::acos(-1.0) / 180.0;
    return axis * ((motor.flipped_bike ? 1.0 : -1.0) * std::cos(angle)) +
           rotate_90deg(axis) * std::sin(angle);
}

vect2 handlebar_position(const motorst& motor) {
    vect2 axis(std::cos(motor.bike.rotation), std::sin(motor.bike.rotation));
    return motor.bike.r + axis * (motor.flipped_bike ? 0.65 : -0.65) +
           rotate_90deg(axis) * 0.35;
}

vect2 muzzle_position(const motorst& motor) {
    vect2 origin = handlebar_position(motor);
    vect2 aim = aim_direction(motor);
    double clearance = std::max({0.3,
        (motor.left_wheel.r - origin) * aim + motor.left_wheel.radius,
        (motor.right_wheel.r - origin) * aim + motor.right_wheel.radius,
        (motor.head_r - origin) * aim + HeadRadius});
    return origin + aim * (clearance + RADIUS + 0.05);
}

bool sweep_circle_segment(vect2 start, vect2 delta, double radius, vect2 a, vect2 b,
                          double& fraction) {
    // A circle swept against an edge is a point swept against a capsule:
    // two endpoint circles plus the edge's offset parallel faces.
    double earliest = 2.0;
    double t;
    if (sweep_point(start, delta, a, radius, t)) {
        earliest = t;
    }
    if (sweep_point(start, delta, b, radius, t)) {
        earliest = std::min(earliest, t);
    }
    vect2 edge = b - a;
    double length = edge.length();
    if (length > 1e-12) {
        vect2 tangent = edge * (1.0 / length);
        vect2 normal = rotate_90deg(tangent);
        double distance = (start - a) * normal;
        double along = (start - a) * tangent;
        if (std::abs(distance) <= radius && along >= 0.0 && along <= length) {
            earliest = 0.0;
        }
        double speed = delta * normal;
        if (std::abs(speed) > 1e-12) {
            for (double side : {-radius, radius}) {
                t = (side - distance) / speed;
                double projected = along + (delta * tangent) * t;
                if (t >= 0.0 && t <= 1.0 && projected >= 0.0 && projected <= length) {
                    earliest = std::min(earliest, t);
                }
            }
        }
    }
    fraction = earliest;
    return earliest <= 1.0;
}

bool spawn(const motorst& motor) {
    if (!Level || !Segments || cooldown_seconds > 1e-12) {
        return false;
    }
    cooldown_seconds = config.delay_seconds;
    vect2 origin = handlebar_position(motor);
    vect2 position = muzzle_position(motor);
    vect2 level_point(position.x, -position.y);
    if (!Level->is_sky(nullptr, &level_point)) {
        return false;
    }
    // Check the entire barrel path too, so an extended muzzle cannot shoot
    // through a wall when the rider is pressed up against it.
    Segments->iterate_all_segments();
    while (segment* seg = Segments->next_segment()) {
        double fraction;
        if (sweep_circle_segment(origin, position - origin, RADIUS + SKIN,
                                 seg->r, seg->r + seg->v, fraction)) {
            return false;
        }
    }
    auto slot = std::find_if(pool.begin(), pool.end(), [](const Shot& s) { return !s.active; });
    if (slot == pool.end()) {
        slot = std::max_element(pool.begin(), pool.end(),
                               [](const Shot& a, const Shot& b) { return a.age_seconds < b.age_seconds; });
    }
    *slot = Shot{};
    slot->active = true;
    slot->position = slot->previous = position;
    slot->velocity = motor.bike.v + aim_direction(motor) * config.speed;
    return true;
}

void update(double dt) {
    if (dt <= 0.0) {
        return;
    }
    double seconds = dt / PHYSICS_PER_SECOND;
    cooldown_seconds = std::max(0.0, cooldown_seconds - seconds);
    for (Shot& shot : pool) {
        if (!shot.active) {
            continue;
        }
        shot.previous = shot.position;
        shot.age_seconds += seconds;
        if (shot.stopped) {
            shot.moving_fraction = 0.0;
            shot.impact_seconds += seconds;
        } else {
            shot.velocity.y -= GRAVITY * dt;
            vect2 delta = shot.velocity * dt;
            double earliest = 1.0;
            bool hit = false;
            Segments->iterate_all_segments();
            while (segment* seg = Segments->next_segment()) {
                double fraction;
                if (sweep_circle_segment(shot.position, delta, RADIUS + SKIN,
                                         seg->r, seg->r + seg->v, fraction)) {
                    earliest = std::min(earliest, fraction);
                    hit = true;
                }
            }
            shot.moving_fraction = earliest;
            shot.position = shot.position + delta * earliest;
            if (hit) {
                shot.stopped = true;
                shot.velocity = vect2();
                shot.impact_seconds = seconds * (1.0 - earliest);
            }
        }
        if ((shot.stopped && shot.impact_seconds >= 3.0 - 1e-10) ||
            (!shot.stopped && shot.age_seconds >= 15.0)) {
            shot.active = false;
        }
    }
}

bool hits_rider(const motorst& current, const motorst& previous) {
    for (const Shot& shot : pool) {
        if (shot.active &&
            (hits_circle(shot, current.head_r, previous.head_r, HeadRadius) ||
             hits_circle(shot, current.left_wheel.r, previous.left_wheel.r, current.left_wheel.radius) ||
             hits_circle(shot, current.right_wheel.r, previous.right_wheel.r, current.right_wheel.radius))) {
            return true;
        }
    }
    return false;
}

bool flash_white(const Shot& shot) {
    // Five black/white cycles per real second. Flashing never disables contact.
    return shot.stopped && shot.impact_seconds >= 2.0 &&
           ((int)((shot.impact_seconds - 2.0) * 10.0) % 2 != 0);
}
} // namespace killer_shot

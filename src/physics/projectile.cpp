#include "physics/projectile.h"
#include "editor/editor.h"
#include "level/level.h"
#include "level/segments.h"
#include "main.h"
#include <algorithm>
#include <cmath>

namespace projectile {
namespace {
std::array<State, MAX_PROJECTILES> squares;
double cooldown = 0.0;
Settings config;
constexpr double SKIN = 0.0001;

// Clip the time interval during which a moving point lies within one slab.
bool slab(double p, double delta, double lo, double hi, double& enter, double& exit) {
    if (std::abs(delta) < 1e-12) {
        return p >= lo && p <= hi;
    }
    double a = (lo - p) / delta;
    double b = (hi - p) / delta;
    if (a > b) {
        std::swap(a, b);
    }
    enter = std::max(enter, a);
    exit = std::min(exit, b);
    return enter <= exit;
}

// Conservative continuous circle/square test: expand the box by the circle
// radius. This catches fast crossings; corners are deliberately square rather
// than rounded in the sweep. Resting contacts below use exact circle geometry.
bool swept_circle(vect2 start, vect2 end, double radius, vect2& normal) {
    const double h = HALF_SIZE + radius;
    if (std::abs(start.x) <= h && std::abs(start.y) <= h) {
        return false; // Existing overlap is handled by circle_contact.
    }
    vect2 d = end - start;
    double enter = 0.0;
    double exit = 1.0;
    if (!slab(start.x, d.x, -h, h, enter, exit) ||
        !slab(start.y, d.y, -h, h, enter, exit)) {
        return false;
    }
    vect2 hit = start + d * enter;
    if (std::abs(std::abs(hit.x) - h) < std::abs(std::abs(hit.y) - h)) {
        normal = vect2(hit.x < 0 ? -1.0 : 1.0, 0);
    } else {
        normal = vect2(0, hit.y < 0 ? -1.0 : 1.0);
    }
    return true;
}
} // namespace

const std::array<State, MAX_PROJECTILES>& states() { return squares; }
Settings& settings() { return config; }
void reset() {
    squares = {};
    cooldown = 0.0;
}

bool sweep_segment(vect2 center, vect2 delta, double half_size, vect2 a, vect2 b,
                   double& fraction) {
    // Continuous separating-axis test: the box's x/y axes and the segment's
    // normal are sufficient. Unlike endpoint tests, this catches thin terrain
    // even when the square crosses the whole edge during a single step.
    const vect2 axes[] = {vect2(1, 0), vect2(0, 1), rotate_90deg(b - a)};
    double enter = 0.0;
    double exit = 1.0;
    for (vect2 axis : axes) {
        double padding = half_size * (std::abs(axis.x) + std::abs(axis.y));
        double p1 = a * axis;
        double p2 = b * axis;
        if (!slab(center * axis, delta * axis, std::min(p1, p2) - padding,
                  std::max(p1, p2) + padding, enter, exit)) {
            return false;
        }
    }
    fraction = enter;
    return true;
}

bool circle_contact(vect2 relative, double radius, vect2& normal, double& depth) {
    vect2 closest(std::clamp(relative.x, -HALF_SIZE, HALF_SIZE),
                  std::clamp(relative.y, -HALF_SIZE, HALF_SIZE));
    vect2 offset = relative - closest;
    double distance = offset.length();
    if (distance > radius + SKIN) {
        return false;
    }
    if (distance > 1e-12) {
        normal = offset * (1.0 / distance);
        depth = std::max(0.0, radius - distance);
    } else {
        // Centre inside the square (including exactly on an edge): use the
        // nearest face, never divide by zero or pass a zero-length anchor.
        bool horizontal = std::abs(relative.x) > std::abs(relative.y);
        normal = horizontal ? vect2(relative.x < 0 ? -1.0 : 1.0, 0)
                            : vect2(0, relative.y < 0 ? -1.0 : 1.0);
        depth = HALF_SIZE + radius - (horizontal ? std::abs(relative.x) : std::abs(relative.y));
    }
    return true;
}

bool spawn(const motorst& motor) {
    if (!Segments || !Level || cooldown > 1e-12) {
        return false;
    }
    // Follow the full chassis orientation, not just its horizontal sign. The
    // unflipped bike faces left; elevate toward the bike's local up in either
    // facing direction. Tilting/turning the bike therefore aims the cannon.
    vect2 axis(std::cos(motor.bike.rotation), std::sin(motor.bike.rotation));
    vect2 facing = axis * (motor.flipped_bike ? 1.0 : -1.0);
    double elevation = config.elevation_degrees * std::acos(-1.0) / 180.0;
    vect2 aim = facing * std::cos(elevation) + rotate_90deg(axis) * std::sin(elevation);
    // Place the muzzle beyond all bike collision circles along the shot axis.
    // The box's projected half-width matters when aiming diagonally.
    double clearance = std::max({(motor.left_wheel.r - motor.bike.r) * aim + motor.left_wheel.radius,
                                 (motor.right_wheel.r - motor.bike.r) * aim + motor.right_wheel.radius,
                                 (motor.head_r - motor.bike.r) * aim + HeadRadius});
    clearance += HALF_SIZE * (std::abs(aim.x) + std::abs(aim.y)) + 0.15;
    vect2 position = motor.bike.r + aim * clearance;
    // Rate-limit even blocked attempts when K is held, avoiding a full terrain
    // scan on every substep while parked against a wall.
    cooldown = config.delay_seconds * 1000.0 * STOPWATCH_MULTIPLIER * STOPWATCH_TO_PHYS_TIME;
    if (config.collide_with_squares) {
        for (const State& other : squares) {
            if (other.active && std::abs(position.x - other.position.x) < 2 * HALF_SIZE + SKIN &&
                std::abs(position.y - other.position.y) < 2 * HALF_SIZE + SKIN) {
                return false; // Never create interpenetrating solid squares.
            }
        }
    }
    vect2 level_point(position.x, -position.y);
    if (!Level->is_sky(nullptr, &level_point)) {
        return false;
    }
    Segments->iterate_all_segments();
    while (segment* seg = Segments->next_segment()) {
        double fraction;
        if (sweep_segment(position, vect2(), HALF_SIZE + SKIN, seg->r, seg->r + seg->v,
                          fraction)) {
            return false;
        }
    }
    auto slot = std::find_if(squares.begin(), squares.end(), [](const State& s) { return !s.active; });
    if (slot == squares.end()) {
        // Bound collision/render cost during sustained fire; recycle oldest.
        slot = std::max_element(squares.begin(), squares.end(),
                                [](const State& a, const State& b) { return a.age < b.age; });
    }
    State& square = *slot;
    square = State{};
    square.active = true;
    square.position = square.previous = position;
    square.velocity = motor.bike.v + aim * config.speed;
    return true;
}

void update(double dt) {
    if (dt <= 0.0) {
        return;
    }
    cooldown = std::max(0.0, cooldown - dt);
    std::array<double, MAX_PROJECTILES> terrain_time;
    terrain_time.fill(2.0); // Absolute fraction of this substep; >1 means no hit.
    for (std::size_t i = 0; i < squares.size(); ++i) {
        State& s = squares[i];
        if (!s.active) {
            continue;
        }
        s.previous = s.position;
        s.age += dt;
        if (s.age > 30.0) {
            s = State{};
            continue;
        }
        if (s.stopped) {
            continue;
        }
        s.velocity.y -= config.gravity * dt;
        Segments->iterate_all_segments();
        while (segment* seg = Segments->next_segment()) {
            double fraction;
            if (sweep_segment(s.position, s.velocity * dt, HALF_SIZE + SKIN,
                              seg->r, seg->r + seg->v, fraction)) {
                terrain_time[i] = std::min(terrain_time[i], fraction);
            }
        }
    }
    // Resolve impacts chronologically. Every event stops at least one moving
    // square, so at most MAX_PROJECTILES events occur. Recompute pair sweeps
    // after a stop: a shot can subsequently hit the newly stationary square.
    // Unchanged moving trajectories retain their precomputed terrain hit time.
    double progress = 0.0;
    for (std::size_t event = 0; event <= MAX_PROJECTILES && progress < 1.0; ++event) {
        double next = 1.0;
        int first = -1;
        int second = -1;
        for (std::size_t i = 0; i < squares.size(); ++i) {
            if (squares[i].active && !squares[i].stopped && terrain_time[i] <= next) {
                next = std::max(progress, terrain_time[i]);
                first = (int)i;
                second = -1;
            }
        }
        if (config.collide_with_squares) {
            for (std::size_t i = 0; i < squares.size(); ++i) {
                if (!squares[i].active) {
                    continue;
                }
                for (std::size_t j = i + 1; j < squares.size(); ++j) {
                    if (!squares[j].active || (squares[i].stopped && squares[j].stopped)) {
                        continue;
                    }
                    vect2 relative_delta = (squares[i].velocity - squares[j].velocity) *
                                           (dt * (1.0 - progress));
                    double fraction;
                    // Minkowski sum of two identical axis-aligned squares.
                    if (sweep_segment(squares[i].position, relative_delta, 2 * HALF_SIZE + SKIN,
                                      squares[j].position, squares[j].position, fraction)) {
                        double hit = progress + fraction * (1.0 - progress);
                        if (hit <= next) {
                            next = hit;
                            first = (int)i;
                            second = (int)j;
                        }
                    }
                }
            }
        }
        for (State& s : squares) {
            if (s.active && !s.stopped) {
                s.position = s.position + s.velocity * (dt * (next - progress));
            }
        }
        progress = next;
        if (first < 0) {
            break;
        }
        squares[first].stopped = true;
        squares[first].velocity = vect2();
        if (second >= 0) {
            squares[second].stopped = true;
            squares[second].velocity = vect2();
        }
    }
}
static void collide_square(const State& square, rigidbody& wheel, vect2 previous_position, double dt) {
    if (!square.active || dt <= 0.0) {
        return;
    }
    vect2 relative = wheel.r - square.position;
    vect2 normal;
    double depth;
    // Prefer entry-side sweep for a fast crossing, even if the end position is
    // inside the box. Otherwise use the closest face/corner of the current box.
    if (swept_circle(previous_position - square.previous, relative, wheel.radius, normal)) {
        double target = HALF_SIZE + wheel.radius;
        depth = std::max(0.0, target - relative * normal);
    } else if (!circle_contact(relative, wheel.radius, normal, depth)) {
        return;
    }
    wheel.r = wheel.r + normal * (depth + SKIN);
    // Infinite-mass object: only the wheel changes velocity. Use the box's
    // actual movement this step, including a step that ended against terrain.
    vect2 surface_velocity = (square.position - square.previous) * (1.0 / dt);
    vect2 relative_velocity = wheel.v - surface_velocity;
    double inward_speed = relative_velocity * normal;
    if (inward_speed < 0.0) {
        wheel.v = wheel.v - normal * inward_speed;
    }
    if (inward_speed <= 0.0) {
        // No-slip tangential impulse couples wheel spin and ground speed.
        // This lets engine torque drive along a stopped square's top face.
        vect2 tangent = rotate_90deg(normal);
        double slip = relative_velocity * tangent - wheel.angular_velocity * wheel.radius;
        double impulse = -slip / (1.0 / wheel.mass + wheel.radius * wheel.radius / wheel.inertia);
        if (SurfaceGrip < 1.0 - 1e-9) {
            double limit = SurfaceGrip * wheel.mass * std::max(0.0, -inward_speed);
            impulse = std::clamp(impulse, -limit, limit);
        }
        wheel.v = wheel.v + tangent * (impulse / wheel.mass);
        wheel.angular_velocity -= impulse * wheel.radius / wheel.inertia;
    }
    wheel.touching_edge = true;
}

void collide_wheel(rigidbody& wheel, vect2 previous_position, double dt) {
    for (const State& square : squares) {
        collide_square(square, wheel, previous_position, dt);
    }
}

bool hits_head(vect2 position, vect2 previous_position, double radius) {
    for (const State& square : squares) {
        if (!square.active) {
            continue;
        }
        vect2 normal;
        double depth;
        if (circle_contact(position - square.position, radius, normal, depth) ||
            swept_circle(previous_position - square.previous, position - square.position, radius,
                         normal)) {
            return true;
        }
    }
    return false;
}
} // namespace projectile

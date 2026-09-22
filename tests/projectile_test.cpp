#include "physics/projectile.h"
#include "level/level.h"
#include "level/segments.h"
#include "main.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

// Fake level source; all motion, sweep and wheel response code under test is
// the production projectile implementation. No window/assets/network needed.
static std::vector<segment> terrain;
static size_t next_edge = 0;
static bool sky = true;
level::level() {}
level::~level() {}
bool level::is_sky(polygon*, vect2*) { return sky; }
segments::segments(level*) {}
segments::~segments() {}
void segments::iterate_all_segments() { next_edge = 0; }
segment* segments::next_segment() {
    return next_edge < terrain.size() ? &terrain[next_edge++] : nullptr;
}
static level test_level;
static segments test_segments(&test_level);
double HeadRadius = 0.238;
double SurfaceGrip = 1.0;
level* Level = &test_level;
segments* Segments = &test_segments;
[[noreturn]] void internal_error(const std::string& message, std::source_location) {
    std::cerr << message << '\n';
    std::abort();
}
static void require(bool pass, const char* label) {
    if (!pass) {
        std::cerr << "FAIL: " << label << '\n';
        std::exit(1);
    }
}
static bool near(double a, double b) { return std::abs(a - b) < 0.001; }

int main() {
    using namespace projectile;
    settings() = Settings{};
    settings().collide_with_squares = false; // Isolate ballistic/lifecycle tests first.
    double fraction;
    require(sweep_segment(vect2(0, 0), vect2(10, 0), 0.5, vect2(5, -5), vect2(5, 5),
                          fraction) && near(fraction, 0.45), "fast sweep through thin wall");
    require(sweep_segment(vect2(0, 3), vect2(0, -6), 0.5, vect2(-5, 0), vect2(5, 0),
                          fraction) && near(fraction, 2.5 / 6.0), "fall onto floor");
    require(!sweep_segment(vect2(0, 3), vect2(10, 0), 0.5, vect2(5, -1), vect2(5, 1),
                           fraction), "clear wall endpoint");
    require(sweep_segment(vect2(0, 3), vect2(0, -6), 0.5, vect2(-5, -5), vect2(5, 5),
                          fraction) && near(fraction, 1.0 / 3.0), "diagonal slope sweep");
    vect2 normal;
    double depth;
    require(circle_contact(vect2(0, 0), 0.6, normal, depth) && near(depth, 1.1) &&
                std::isfinite(normal.x), "centre overlap is finite");
    require(!circle_contact(vect2(1, 1), 0.6, normal, depth), "exact rounded corner rejection");

    motorst motor{};
    motor.flipped_bike = 1;
    motor.bike.r = vect2(0, 1.2);
    motor.left_wheel.r = vect2(-0.85, 0.6);
    motor.right_wheel.r = vect2(0.85, 0.6);
    motor.head_r = vect2(0, 2.24);
    motor.left_wheel.radius = motor.right_wheel.radius = 0.6;
    auto count = [] {
        size_t n = 0;
        for (const auto& s : states()) { n += s.active; }
        return n;
    };
    const double delay = 0.4 * 1000 * STOPWATCH_MULTIPLIER * STOPWATCH_TO_PHYS_TIME;
    require(spawn(motor), "spawn in clear sky");
    const vect2 launch = states()[0].velocity;
    const vect2 origin = states()[0].position;
    require(launch.x > 0 && launch.y > 0 && near(std::atan2(launch.y, launch.x), 15 * std::acos(-1) / 180),
            "15 degree upward launch");
    require(!spawn(motor) && count() == 1, "immediate launch cooldown");
    update(delay * 0.9);
    require(!spawn(motor), "cooldown lasts 0.4 real seconds");
    update(delay * 0.1);
    motor.bike.v = vect2(3, 2);
    require(spawn(motor) && count() == 2, "multiple simultaneous squares");
    require(near(states()[1].velocity.x, launch.x + 3) && near(states()[1].velocity.y, launch.y + 2),
            "inherit both components of bike velocity");
    require(states()[0].position.x > origin.x && states()[0].velocity.y < launch.y,
            "old shot continues under gravity");

    reset();
    motor.bike.v = vect2();
    segment floor{};
    floor.r = vect2(-50, 0);
    floor.v = vect2(100, 0);
    // A half-metre-high obstacle lies ahead of the muzzle.
    segment obstacle{};
    obstacle.r = vect2(3.0, 0);
    obstacle.v = vect2(0, 0.5);
    terrain = {floor, obstacle};
    require(spawn(motor), "flat-ground cannon spawn");
    double peak = states()[0].position.y;
    for (int i = 0; i < 1000 && !states()[0].stopped; ++i) {
        update(0.0055);
        peak = std::max(peak, states()[0].position.y);
    }
    require(states()[0].stopped && states()[0].position.x > 5.5 && states()[0].position.x < 6.7,
            "stationary flat-ground range roughly 20 feet past bike");
    require(peak > origin.y + 0.1 && near(states()[0].position.y, HALF_SIZE),
            "rises over small obstacle then lands under gravity");
    std::cout << "Stationary range: " << states()[0].position.x << " metres from bike\n";
    vect2 stopped = states()[0].position;
    update(0.1);
    require((states()[0].position - stopped).length() == 0, "stays stopped at terrain");
    rigidbody wheel{};
    wheel.radius = 0.6;
    wheel.mass = 10;
    wheel.inertia = 0.72;
    wheel.r = stopped + vect2(0, 1.0);
    wheel.v = vect2(0, -2);
    collide_wheel(wheel, stopped + vect2(0, 1.2), 0.1);
    require(wheel.r.y >= stopped.y + 1.1 && wheel.v.y >= 0 && wheel.touching_edge,
            "wheel rests on landed shot");
    wheel.r = stopped;
    wheel.v = vect2();
    collide_wheel(wheel, stopped, 0.1);
    require(std::isfinite(wheel.r.y) && (wheel.r - stopped).length() >= 1.1,
            "embedded wheel remains finite");
    require(hits_head(stopped, stopped, 0.238), "head detects landed shot");

    reset();
    terrain.clear();
    motor.flipped_bike = 0;
    require(spawn(motor) && states()[0].velocity.x < 0 && states()[0].velocity.y > 0,
            "left-facing shot also elevated");
    reset();
    motor.flipped_bike = 1;
    motor.bike.rotation = std::acos(-1) / 2;
    require(spawn(motor) && states()[0].velocity.y > 5 && states()[0].velocity.x < 0,
            "cannon follows full bike rotation");
    reset();
    motor.bike.rotation = 0;
    // Exact cancellation of muzzle velocity must still accelerate under gravity.
    motor.bike.v = launch * -1;
    require(spawn(motor), "cancelled velocity spawn");
    update(0.01);
    require(states()[0].velocity.y < 0 && !states()[0].stopped,
            "zero initial speed does not freeze projectile");

    reset();
    motor.bike.v = vect2(100, 0);
    require(spawn(motor), "fast inherited velocity spawn");
    vect2 fast_origin = states()[0].position;
    update(0.1);
    wheel.r = fast_origin + vect2(2, 0);
    wheel.v = vect2();
    wheel.angular_velocity = 0;
    collide_wheel(wheel, wheel.r, 0.1);
    require(wheel.r.x > states()[0].position.x && wheel.v.x > 100,
            "fast square pushes wheel from entry side");
    require(hits_head(fast_origin + vect2(2, 0), fast_origin + vect2(2, 0), 0.238),
            "fast square crossing head");

    reset();
    motor.bike.v = vect2();
    for (size_t i = 0; i < MAX_PROJECTILES + 3; ++i) {
        require(spawn(motor), "sustained fire recycles oldest at limit");
        update(delay);
    }
    require(count() == MAX_PROJECTILES && states()[3].age > states()[0].age,
            "bounded pool recycles oldest, retaining other shots");
    require(hits_head(states()[30].position, states()[30].position, 0.238),
            "collision checks later pool entries");
    update(31.0);
    require(count() == 0, "all expired shots removed");
    reset();
    sky = false;
    require(!spawn(motor), "reject spawn in solid terrain");
    sky = true;
    update(delay);
    require(spawn(motor), "blocked-shot cooldown recovers");
    segment wall{};
    wall.r = states()[0].position + vect2(0.25, -1);
    wall.v = vect2(0, 2);
    terrain = {wall};
    update(delay);
    require(!spawn(motor), "reject muzzle crossing terrain edge");
    reset();
    require(count() == 0, "reset clears all shots and cooldown");
    terrain.clear();
    settings() = Settings{};
    settings().gravity = 0.0;
    settings().elevation_degrees = 0.0;
    require(near(settings().delay_seconds, 0.4), "default cooldown is 0.4 seconds");
    motor.bike.v = vect2();
    motor.bike.rotation = 0;
    motor.flipped_bike = 1;
    auto translate = [&](double x) {
        motor.bike.r.x += x;
        motor.left_wheel.r.x += x;
        motor.right_wheel.r.x += x;
        motor.head_r.x += x;
    };
    require(spawn(motor), "first head-on shot");
    update(delay);
    translate(10);
    motor.flipped_bike = 0;
    require(spawn(motor), "second head-on shot");
    update(1.0);
    require(states()[0].stopped && states()[1].stopped &&
                near(states()[1].position.x - states()[0].position.x, 2 * HALF_SIZE + 0.0001),
            "head-on squares both stop without overlap or tunnelling");
    update(delay);
    // Translate the left-facing muzzle onto an existing square.
    double original_muzzle_x = motor.bike.r.x - 2.1;
    double offset = states()[1].position.x - original_muzzle_x;
    translate(offset);
    require(!spawn(motor), "reject muzzle overlapping another square");
    translate(-offset - 10);
    motor.flipped_bike = 1;

    reset();
    wall.r = vect2(5, -10);
    wall.v = vect2(0, 20);
    terrain = {wall};
    require(spawn(motor), "terrain target spawn");
    update(1.0);
    require(states()[0].stopped, "first shot parked against wall");
    require(spawn(motor), "second shot toward parked target");
    update(1.0);
    require(states()[1].stopped && states()[1].position.x < states()[0].position.x &&
                near(states()[0].position.x - states()[1].position.x, 2 * HALF_SIZE + 0.0001),
            "second square stops at parked square before terrain");

    reset();
    terrain.clear();
    settings().collide_with_squares = false;
    require(spawn(motor), "pass-through first shot");
    update(delay);
    translate(10);
    motor.flipped_bike = 0;
    require(spawn(motor), "pass-through opposing shot");
    update(1.0);
    require(!states()[0].stopped && !states()[1].stopped &&
                states()[0].position.x > states()[1].position.x,
            "pass-through option restores noninteracting shots");
    settings() = Settings{};
    reset();
    std::cout << "PASS: multiple shots, cooldown, aim, inherited velocity, gravity, range, collisions, lifecycle\n";
}

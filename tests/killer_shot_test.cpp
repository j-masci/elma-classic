#include "physics/killer_shot.h"
#include "level/level.h"
#include "level/segments.h"
#include "main.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

// Terrain source only is mocked; sweeps, ballistics, aim and lifecycle are real.
static std::vector<segment> terrain;
static std::size_t cursor;
static bool sky = true;
level::level() {}
level::~level() {}
bool level::is_sky(polygon*, vect2*) { return sky; }
segments::segments(level*) {}
segments::~segments() {}
void segments::iterate_all_segments() { cursor = 0; }
segment* segments::next_segment() {
    return cursor < terrain.size() ? &terrain[cursor++] : nullptr;
}
static level test_level;
static segments test_segments(&test_level);
level* Level = &test_level;
segments* Segments = &test_segments;
double HeadRadius = 0.238;
[[noreturn]] void internal_error(const std::string& message, std::source_location) {
    std::cerr << message;
    std::abort();
}
static void require(bool value, const char* label) {
    if (!value) {
        std::cerr << "FAIL: " << label << '\n';
        std::exit(1);
    }
}
static bool near(double a, double b) { return std::abs(a - b) < 0.00001; }

int main() {
    using namespace killer_shot;
    constexpr double TIME_SCALE = 1000 * STOPWATCH_MULTIPLIER * STOPWATCH_TO_PHYS_TIME;
    double fraction;
    require(sweep_circle_segment(vect2(0, 0), vect2(10, 0), RADIUS,
                                  vect2(5, -1), vect2(5, 1), fraction) && near(fraction, 0.488),
            "fast circle stops at thin wall");
    require(!sweep_circle_segment(vect2(4.9, 1.1), vect2(), RADIUS,
                                   vect2(5, -1), vect2(5, 1), fraction),
            "rounded endpoint is not a bounding box");
    require(sweep_circle_segment(vect2(5, 0), vect2(), RADIUS,
                                  vect2(5, -1), vect2(5, 1), fraction) && fraction == 0,
            "stationary overlap handled without division by zero");
    require(sweep_circle_segment(vect2(0, 0), vect2(10, 0), RADIUS,
                                  vect2(5, 0), vect2(5, 0), fraction), "degenerate edge endpoint");

    motorst rider{};
    rider.flipped_bike = 1;
    rider.bike.r = vect2(0, 1.2);
    rider.left_wheel.r = vect2(-0.85, 0.6);
    rider.right_wheel.r = vect2(0.85, 0.6);
    rider.left_wheel.radius = rider.right_wheel.radius = 0.6;
    rider.head_r = vect2(0, 2.24);
    rotate_aim(10 - aim_degrees());
    vect2 aim = aim_direction(rider);
    rider.flipped_bike = 0;
    vect2 flipped = aim_direction(rider);
    require(near(flipped.x, -aim.x) && near(flipped.y, aim.y), "turn mirrors aiming line");
    rider.flipped_bike = 1;
    rider.bike.rotation = std::acos(-1) / 2;
    require(near(aim_direction(rider).x, -aim.y) && near(aim_direction(rider).y, aim.x),
            "aim follows chassis rotation");
    rider.bike.rotation = 0;
    rotate_aim(60);
    require(near(aim_degrees(), 70), "aim adjustment");
    rotate_aim(-60);
    settings() = Settings{};
    rider.bike.v = vect2(3, 2);
    require(spawn(rider), "clear muzzle spawn");
    require(!hits_rider(rider, rider), "shot starts outside shooter's collision circles");
    require(near(shots()[0].velocity.x, 3 + 24 * aim.x) &&
                near(shots()[0].velocity.y, 2 + 24 * aim.y), "inherited velocity");
    require(!spawn(rider), "cooldown blocks immediate repeat");
    double initial_y_speed = shots()[0].velocity.y;
    update(0.24 * TIME_SCALE);
    require(!spawn(rider), "delay measured in real seconds");
    require(shots()[0].velocity.y < initial_y_speed, "mild gravity during flight");
    update(0.01 * TIME_SCALE);
    require(spawn(rider) && shots()[1].active, "multiple shots after cooldown");

    reset();
    rider.bike.v = vect2(100, 0);
    require(spawn(rider), "high-speed shooter spawn");
    motorst fast_rider = rider;
    update(0.0055);
    vect2 travel = rider.bike.v * 0.0055;
    fast_rider.head_r = fast_rider.head_r + travel;
    fast_rider.left_wheel.r = fast_rider.left_wheel.r + travel;
    fast_rider.right_wheel.r = fast_rider.right_wheel.r + travel;
    require(!hits_rider(fast_rider, rider), "same-step shot movement keeps fast shooter clear");

    reset();
    rotate_aim(-aim_degrees());
    rider.bike.v = vect2();
    require(spawn(rider), "fast collision shot");
    vect2 origin = shots()[0].position;
    update(0.2);
    motorst victim = rider;
    victim.head_r = origin + vect2(2, 0);
    victim.left_wheel.r = victim.right_wheel.r = vect2(-50, -50);
    require(hits_rider(victim, victim), "swept head hit between endpoints");
    victim.head_r = vect2(-50, -50);
    victim.left_wheel.r = origin + vect2(2, 0);
    require(hits_rider(victim, victim), "wheel contact lethal like native killers");

    reset();
    require(spawn(rider), "terrain impact shot");
    segment wall{};
    wall.r = vect2(origin.x + 2, -10);
    wall.v = vect2(0, 20);
    terrain = {wall};
    update(0.2);
    require(shots()[0].stopped && near(shots()[0].position.x, wall.r.x - RADIUS - 0.0001),
            "stop at terrain surface");
    vect2 stopped = shots()[0].position;
    victim.head_r = stopped;
    victim.left_wheel.r = victim.right_wheel.r = vect2(-50, -50);
    require(hits_rider(victim, victim), "stationary hazard lethal");
    require(!flash_white(shots()[0]), "black before final second");
    update((2.01 - shots()[0].impact_seconds) * TIME_SCALE);
    require(shots()[0].active && near(shots()[0].impact_seconds, 2.01) &&
                near(shots()[0].position.x, stopped.x), "impact timer and fixed location");
    bool first_phase = flash_white(shots()[0]);
    update(0.1 * TIME_SCALE);
    require(flash_white(shots()[0]) != first_phase && hits_rider(victim, victim),
            "black/white flash does not disable collision");
    update((2.99 - shots()[0].impact_seconds) * TIME_SCALE);
    require(shots()[0].active, "alive just before three seconds");
    update(0.01 * TIME_SCALE);
    require(!shots()[0].active && !hits_rider(victim, victim), "gone at three seconds after impact");

    reset();
    wall.r.x = handlebar_position(rider).x + 0.1;
    terrain = {wall};
    require(!spawn(rider), "barrel cannot extend through thin wall");
    reset();
    terrain.clear();
    sky = false;
    require(!spawn(rider), "reject muzzle in ground");
    sky = true;
    reset();
    require(spawn(rider), "inflight lifetime spawn");
    update(15.0 * TIME_SCALE);
    require(!shots()[0].active, "stray shots expire after fifteen real seconds");
    reset();
    settings().delay_seconds = 0.01;
    for (std::size_t i = 0; i < MAX_SHOTS + 3; ++i) {
        require(spawn(rider), "bounded pool accepts sustained fire");
        update(0.01 * TIME_SCALE);
    }
    std::size_t active = 0;
    for (const auto& s : shots()) { active += s.active; }
    require(active == MAX_SHOTS && shots()[3].age_seconds > shots()[0].age_seconds,
            "oldest shot recycled at cap");
    reset();
    for (const auto& s : shots()) { require(!s.active, "reset clears hazards"); }
    std::cout << "PASS: aim/turning, cooldown, inheritance, gravity, sweeps, rider hits, impact timer, flash and reset\n";
}

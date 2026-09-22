#include "physics/init.h"
#include "level/segments.h"
#include "main.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

static motorst motors[2];
motorst* Motor1 = &motors[0];
motorst* Motor2 = &motors[1];
double HeadRadius = 0.238;
// Level is opaque to tuning; the fake segment builder only records its input.
static int level_token;
level* Level = reinterpret_cast<level*>(&level_token);
static int rebuilds = 0;
static double grid_radius = 0;
segments::segments(level*) { ++rebuilds; }
segments::~segments() {}
void segments::setup_collision_grid(double radius) { grid_radius = radius; }
segments* Segments = nullptr;
[[noreturn]] void internal_error(const std::string& message, std::source_location) {
    std::cerr << message << '\n';
    std::abort();
}

static void require(bool pass, const char* message) {
    if (!pass) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
static bool near(double a, double b) { return std::abs(a - b) < 1e-9; }

int main() {
    require(near(WheelSizeScale,1.0) && near(ThrottlePowerScale,1.0) && near(SurfaceGrip,1.0),
            "startup uses normal wheels, throttle and traction");
    for (auto& motor : motors) {
        for (auto* wheel : {&motor.left_wheel, &motor.right_wheel}) {
            wheel->radius = 0.6;
            wheel->inertia = 0.72;
            wheel->angular_velocity = 10;
            wheel->r = vect2(2, 3);
            wheel->v = vect2(4, 5);
        }
    }
    Segments = new segments(Level);
    set_wheel_size_scale(1.6);
    require(rebuilds == 2 && near(grid_radius, 0.64), "growing wheels rebuild collision lookup");
    for (auto& motor : motors) {
        for (auto* wheel : {&motor.left_wheel, &motor.right_wheel}) {
            require(near(wheel->radius, 0.64) && near(wheel->inertia, 0.8192),
                    "both players get matching radius and inertia");
            require(near(wheel->angular_velocity * wheel->radius, 6.0), "preserve tread speed");
            require(near(wheel->r.x, 2) && near(wheel->v.y, 5), "do not reset bike state");
        }
    }
    set_wheel_size_scale(1.6);
    require(rebuilds == 2, "unchanged size does not rebuild");
    set_wheel_size_scale(100);
    require(near(WheelSizeScale, 3.0) && near(grid_radius, 1.2), "upper wheel limit");
    set_wheel_size_scale(-1);
    require(near(WheelSizeScale, 0.5) && near(grid_radius, HeadRadius), "head dominates small-wheel grid");
    set_wheel_size_scale(std::numeric_limits<double>::quiet_NaN());
    require(near(WheelSizeScale, 0.5), "ignore invalid scale");
    set_throttle_power_scale(1.25);
    require(near(ThrottlePowerScale, 1.25) && near(WheelSizeScale, 0.5), "power independent of size");
    set_throttle_power_scale(999);
    require(near(ThrottlePowerScale, 5.0), "upper power limit");
    set_throttle_power_scale(-999);
    require(near(ThrottlePowerScale, 0.25), "lower power limit");
    set_surface_grip(0.4);
    require(near(SurfaceGrip, 0.4), "traction adjustable independently");
    set_surface_grip(-1);
    require(near(SurfaceGrip, 0), "traction lower bound");
    set_surface_grip(2);
    require(near(SurfaceGrip, 1), "traction upper bound");
    set_surface_grip(std::numeric_limits<double>::quiet_NaN());
    require(near(SurfaceGrip, 1), "ignore invalid traction");
    delete Segments;
    Segments = nullptr;
    set_wheel_size_scale(1.5);
    require(near(WheelSizeScale, 1.5), "menu tuning without a loaded collision grid");
    std::cout << "PASS: live radius/inertia, grid rebuild, tread speed, bounds and independent power\n";
}

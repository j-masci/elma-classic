#include "physics/init.h"
#include "editor/editor.h"
#include "level/segments.h"
#include <algorithm>
#include <cmath>

double WheelSizeScale = 1.0;
double ThrottlePowerScale = 1.0;
double SurfaceGrip = 1.0;

void set_surface_grip(double grip) {
    if (std::isfinite(grip)) {
        SurfaceGrip = std::clamp(grip, 0.0, 1.0);
    }
}

void set_throttle_power_scale(double scale) {
    if (std::isfinite(scale)) {
        ThrottlePowerScale = std::clamp(scale, 0.25, 5.0);
    }
}

void set_wheel_size_scale(double scale) {
    if (!std::isfinite(scale)) {
        return;
    }
    scale = std::clamp(scale, 0.5, 3.0);
    if (std::abs(scale - WheelSizeScale) < 1e-9) {
        return;
    }
    WheelSizeScale = scale;
    for (motorst* motor : {Motor1, Motor2}) {
        for (rigidbody* wheel : {&motor->left_wheel, &motor->right_wheel}) {
            double radius = STANDARD_WHEEL_RADIUS * scale;
            // Preserve tread speed instead of injecting a jump in rolling speed.
            wheel->angular_velocity *= wheel->radius / radius;
            wheel->radius = radius;
            wheel->inertia = 0.32 * scale * scale;
        }
    }
    if (Segments && Level) {
        // The old broad phase is padded for the OLD wheel radius. Recreate it
        // before the next substep so growing wheels cannot miss nearby edges.
        delete Segments;
        Segments = new segments(Level);
        Segments->setup_collision_grid(std::max(HeadRadius, STANDARD_WHEEL_RADIUS * scale));
    }
}

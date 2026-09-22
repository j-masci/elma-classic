#ifndef PHYSICS_INIT_H
#define PHYSICS_INIT_H

#include "vect2.h"

// Session-only experiments, shared by the options menu and live hotkeys.
// Replays do not store these values. Resizing rebuilds the terrain lookup.
extern double WheelSizeScale;
extern double ThrottlePowerScale;
extern double SurfaceGrip;
void set_surface_grip(double grip);
void set_wheel_size_scale(double scale);
void set_throttle_power_scale(double scale);
inline constexpr double STANDARD_WHEEL_RADIUS = 0.4;

extern double GroundEscapeVelocity;
extern double WheelDeformationLength;

extern double Gravity;

extern double TwoPointDiscriminationDistance;

extern double VoltDelay;
extern double LevelEndDelay;

extern double SpringTensionCoefficient;
extern double SpringResistanceCoefficient;

extern double HeadRadius;

extern double ObjectRadius;

extern double MetersToPixels, PixelsToMeters;

extern double LeftWheelDX, LeftWheelDY, RightWheelDX, RightWheelDY, BodyDY;

extern int MinimapScaleFactor;
extern double MetersToMinimapPixels;

enum class MotorGravity {
    Up = 0,
    Down = 1,
    Left = 2,
    Right = 3,
};

// Physics coordinates are metres, with +y upwards (level files use +y down).
// r/v are centre position/linear velocity; angles are radians, and inertia
// controls how much angular acceleration a torque produces (alpha = torque/I).
struct rigidbody {
    double rotation;
    double angular_velocity;
    double radius;
    double mass;
    double inertia; // Moment of inertia
    vect2 r;
    vect2 v;

    bool touching_edge; // true if wheel is touching polygon edge
};

// Impulse-limited tyre friction for the experimental sliding mode.
void apply_surface_friction(rigidbody& wheel, vect2 tangent, double normal_impulse);

// A bike is three rigid bodies coupled by spring/damper forces: chassis and
// two wheels. Only wheels resolve terrain contacts. The rider is a separate
// spring-driven point; head_r is derived from it and touching terrain kills.
struct motorst {
    rigidbody bike;
    rigidbody left_wheel;
    rigidbody right_wheel;
    vect2 head_r;
    int flipped_bike;
    MotorGravity gravity_direction;

    vect2 body_r;
    vect2 body_v;

    int apple_count;
    int last_apple_time;
    int apple_bug_count;

    bool one_wheel_failed;

    bool prev_brake;
    double left_wheel_brake_rotation;
    double right_wheel_brake_rotation;

    bool volting_right;
    bool volting_left;
    double right_volt_time;
    double left_volt_time;
    double angular_velocity_pre_right_volt;
    double angular_velocity_pre_left_volt;
    // Fraction of temporary volt spin remaining after water drag.
    double right_volt_water_scale = 1.0;
    double left_volt_water_scale = 1.0;
};

extern motorst *Motor1, *Motor2;

void set_zoom_factor();
void set_minimap_zoom_factor();
void init_physics_data();
void init_motor(motorst* motor);

#endif

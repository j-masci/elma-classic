#include "physics/init.h"
#include <algorithm>

void apply_surface_friction(rigidbody& wheel, vect2 tangent, double normal_impulse) {
    double slip = wheel.v * tangent - wheel.angular_velocity * wheel.radius;
    double impulse = -slip / (1.0 / wheel.mass + wheel.radius * wheel.radius / wheel.inertia);
    double limit = SurfaceGrip * std::max(0.0, normal_impulse);
    impulse = std::clamp(impulse, -limit, limit);
    wheel.v = wheel.v + tangent * (impulse / wheel.mass);
    wheel.angular_velocity -= impulse * wheel.radius / wheel.inertia;
}

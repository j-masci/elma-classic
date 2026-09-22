#include "physics/rain.h"
#include "physics/init.h"
#include <cmath>
#include <algorithm>

void rain::apply_water_drag(motorst& motor, double dt) {
    if (!std::isfinite(dt) || dt<=0) return;
    double resistance=settings().water_resistance;
    resistance=std::isfinite(resistance)?std::clamp(resistance,0.0,1.0):0.35;
    // Exponential damping is stable even for a large step and cannot reverse
    // velocity. Submersion scales resistance in every direction, including
    // sinking. Gravity stays unchanged, so water slows falls rather than lifts.
    auto damp=[&](rigidbody& body) {
        double wet=submerged_fraction(body.r,body.radius);
        body.v=body.v*std::exp(-8.0*resistance*wet*dt);
        double angular=std::exp(-5.0*resistance*wet*dt);
        body.angular_velocity*=angular;
        return angular;
    };
    damp(motor.left_wheel);
    damp(motor.right_wheel);
    double angular=damp(motor.bike);
    motor.body_v=motor.body_v*std::exp(-8.0*resistance*submerged_fraction(motor.body_r,0.25)*dt);
    // The delayed volt correction must remove only the remaining temporary
    // boost and compare with the damped baseline. Otherwise it restores spin
    // already lost to water, or subtracts a boost that no longer exists.
    if (motor.volting_right) {
        motor.angular_velocity_pre_right_volt*=angular;
        motor.right_volt_water_scale*=angular;
    }
    if (motor.volting_left) {
        motor.angular_velocity_pre_left_volt*=angular;
        motor.left_volt_water_scale*=angular;
    }
}

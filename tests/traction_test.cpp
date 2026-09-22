#include "physics/init.h"
#include "physics/move.h"
#include "game/recorder.h"
#include "main.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
void internal_error(const std::string& message, std::source_location) { std::cerr << message; std::abort(); }
double SurfaceGrip=1,GroundEscapeVelocity=0.01,WheelDeformationLength=0.001;
double Gravity=10,SpringTensionCoefficient=1,SpringResistanceCoefficient=1,BodyDY=0;
int get_two_anchor_points(vect2 r,double radius,vect2* a,vect2*) {
    if (r.y>radius+0.001) return 0;
    *a=vect2(r.x,0); return 1;
}
void add_event_buffer(WavEvent,double,int) {}
void require(bool b,const char* s) { if(!b) { std::cerr<<s<<'\n'; std::exit(1); } }
rigidbody wheel() { rigidbody w{}; w.mass=2; w.inertia=1; w.radius=1; w.r={0,1}; w.v={5,0}; return w; }
int main() {
    auto ice=wheel(); SurfaceGrip=0;
    rigidbody_movement(&ice,{0,-20},100,0.1,true);
    require(std::abs(ice.v.x-5)<1e-10 && ice.angular_velocity>0,"ice lets wheel spin independently of translation");
    auto partial=wheel(); SurfaceGrip=0.5;
    rigidbody_movement(&partial,{0,-20},0,0.1,true);
    require(partial.v.x<5 && partial.v.x>=4.5-1e-10,"friction limited by normal load");
    auto normal=wheel(); SurfaceGrip=1;
    rigidbody_movement(&normal,{0,-20},0,0.1,true);
    require(std::abs(normal.v.x-5)<1e-10 && std::abs(normal.angular_velocity+5)<1e-10,"full grip preserves original rolling");
    auto air=wheel(); air.r.y=10; SurfaceGrip=0;
    rigidbody_movement(&air,{0,-20},0,0.1,true);
    require(std::abs(air.v.y+1)<1e-10,"ice does not disable gravity");
    std::cout<<"PASS ice sliding, bounded friction, default rolling, gravity\n";
}

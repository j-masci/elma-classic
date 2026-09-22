#ifndef PHYSICS_RAIN_H
#define PHYSICS_RAIN_H
#include "vect2.h"
#include <array>
#include <vector>
class level;
class pic8;
struct motorst;
namespace rain {
constexpr int MAX_DROPS = 30000;
struct Drop {
    bool active = false;
    vect2 position, velocity; // Position is ALWAYS the bottom contact point.
    int edge = -1;
    int flat_direction = 0; // Overflow keeps moving across a level spillway.
};
struct Settings {
    double frequency = 1.0; // drops/metre/real second; zero disables emission
    double water_resistance = 0.35; // 1 = original heavy drag, 0 = visual water only
};
// A terrain profile bounded by two rims. Area is 2D water volume.
struct Puddle {
    vect2 bottom, bottom_right, left, right; // Bottom endpoints coincide for a V.
    double capacity = 0, area = 0, overflow_area = 0;
    std::vector<vect2> floor; // Ordered left to right, including bank segments.
    double surface = 0;
    std::vector<vect2> full_floor; // Preserve higher banks for later lake merging.
    int outside_left = -1, outside_right = -1;
};
constexpr double DROP_AREA = 0.01;
const std::vector<Puddle>& puddles();
double water_height(const Puddle& puddle);
void water_span(const Puddle& puddle, double y, double& left, double& right);
// Separate wet intervals (x = left, y = right), clipped to actual sky/terrain.
void water_spans(const Puddle& puddle, double y, std::vector<vect2>& spans);
double submerged_fraction(vect2 center, double radius);
bool fully_submerged(vect2 center, double radius);
struct Bubble { bool active=false; vect2 position; double age=0, phase=0, radius=0.025; };
const std::array<Bubble,128>& bubbles();
void breathe(int player, vect2 head, double radius, bool alive, double physics_dt);
void apply_water_drag(motorst& motor, double dt);
Settings& settings();
const std::array<Drop, MAX_DROPS>& drops();
int drop_count();
using ImpactListener = void (*)(vect2 position, double seconds, bool water);
void set_impact_listener(ImpactListener listener);
void initialize(const level& lev);
void set_scene(const std::vector<std::vector<vect2>>& polygons);
void clear(); // Keep geometry and session settings.
bool add_drop(vect2 point);
void update(double physics_dt);
double source_length();
double radius(const Drop& drop);
void render(pic8& pic, vect2 bottomleft, double scale, const unsigned char* palette);
void render_breathing(pic8& pic,vect2 head,double radius,vect2 bottomleft,double scale,unsigned char white);
}
#endif

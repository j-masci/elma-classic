#include "physics/rain.h"
#include "level/level.h"
#include "level/polygon.h"
void rain::initialize(const level& lev) {
    std::vector<std::vector<vect2>> polygons;
    for (auto* p:lev.polygons) {
        if (!p || p->is_grass) continue;
        std::vector<vect2> vertices;
        for (int i=0;i<p->vertex_count;++i) vertices.emplace_back(p->vertices[i].x,-p->vertices[i].y);
        polygons.push_back(std::move(vertices));
    }
    set_scene(polygons);
}

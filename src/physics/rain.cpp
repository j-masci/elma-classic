#include "physics/rain.h"
#include "main.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <unordered_map>

namespace rain {
namespace {
struct Edge { vect2 a, b; int next, prev; bool support = false; };
struct Source { int edge; double cumulative; };
std::vector<Edge> edges;
std::vector<Source> sources;
std::array<Drop, MAX_DROPS> pool;
std::array<Bubble,128> bubble_pool;
std::array<double,2> breathing_time{};
unsigned bubble_cursor=0;
std::vector<int> free_slots;
using Grid = std::unordered_map<uint64_t, std::vector<int>>;
Grid terrain;
std::vector<Puddle> basins;
struct Outlet { vect2 start, velocity; int edge, flat_direction; };
std::vector<std::vector<Outlet>> outlets;
std::vector<unsigned> next_outlet;
std::vector<Puddle> initial_basins;
std::vector<std::vector<Outlet>> initial_outlets;
std::vector<int> initial_edge_basin;
std::vector<int> edge_basin;
std::unordered_map<int,std::vector<int>> basin_columns;
std::unordered_map<int,std::vector<int>> sky_rows;
std::vector<unsigned> visited;
unsigned generation = 0;
Settings config;
ImpactListener impact_listener = nullptr;
double simulation_seconds = 0;
std::mt19937 random(1337);
double total_length = 0, emission = 0, min_y = 0, pending_time = 0;
constexpr double CELL = 2.0;
// Level vertices are not perfectly vertical. Treat sub-3-cm wall leans
// within about one degree as vertical for WATER only, never bike collision.
constexpr double WALL_LEAN_RATIO = 0.02;
constexpr double WALL_LEAN_DISTANCE = 0.03;
constexpr double REAL_SCALE = 1000.0 * STOPWATCH_MULTIPLIER * STOPWATCH_TO_PHYS_TIME;
double cross(vect2 a, vect2 b) { return a.x*b.y-a.y*b.x; }
int cell(double x) { return (int)std::floor(x/CELL); }
uint64_t key(int x, int y) { return (uint64_t(uint32_t(x)) << 32) | uint32_t(y); }
template<class F> void query(Grid& grid, vect2 a, vect2 b, F f) {
    for (int x=cell(std::min(a.x,b.x)); x<=cell(std::max(a.x,b.x)); ++x)
        for (int y=cell(std::min(a.y,b.y)); y<=cell(std::max(a.y,b.y)); ++y) {
            auto it=grid.find(key(x,y));
            if (it!=grid.end()) for (int id:it->second) f(id);
        }
}
bool sky(vect2 p) {
    // Odd-even fill matches the level's alternating sky/ground polygons.
    bool inside=false;
    auto row=sky_rows.find(cell(p.y));
    if (row==sky_rows.end()) return false;
    for (int id:row->second) {
        const auto& e=edges[id];
        if ((e.a.y>p.y)!=(e.b.y>p.y) &&
            p.x < e.a.x+(p.y-e.a.y)*(e.b.x-e.a.x)/(e.b.y-e.a.y)) inside=!inside;
    }
    return inside;
}
void release(int id) { pool[id].active=false; free_slots.push_back(id); }
double area_below(const Puddle& p,double y) {
    double area=0;
    for (std::size_t i=1;i<p.floor.size();++i) {
        vect2 a=p.floor[i-1],b=p.floor[i];
        double width=b.x-a.x, da=y-a.y, db=y-b.y;
        if (da>=0 && db>=0) area+=width*(da+db)*0.5;
        else if (da>0 || db>0) {
            double depth=std::max(da,db);
            area+=width*depth*depth/(2*std::abs(b.y-a.y));
        }
    }
    return area;
}
void update_surface(Puddle& p) {
    double low=p.bottom.y,high=p.left.y;
    if (p.area>=p.capacity) { p.surface=high; return; }
    // Monotone area-versus-height for any single pit. Cache this result;
    // rendering and bike substeps only read it, never repeat the search.
    for (int i=0;i<36;++i) {
        double middle=(low+high)*0.5;
        if (area_below(p,middle)<p.area) low=middle; else high=middle;
    }
    p.surface=(low+high)*0.5;
}
void rebuild_basin_columns() {
    basin_columns.clear();
    for (int n=0;n<(int)basins.size();++n) if (basins[n].capacity>0)
        for (int x=cell(basins[n].left.x);x<=cell(basins[n].right.x);++x)
            basin_columns[x].push_back(n);
}
void rebuild_outlets(int n) {
    auto& p=basins[n]; outlets[n].clear(); next_outlet[n]=0;
    auto add=[&](int edge,vect2 rim_point,int side) {
        if (edge<0 || std::abs(rim_point.y-p.left.y)>1e-8) return;
        const auto& outside=edges[edge];
        bool forward=(outside.a-rim_point).length()<1e-7;
        vect2 end=forward?outside.b:outside.a;
        double length=(end-rim_point).length();
        if (end.y>rim_point.y+1e-8 || length<1e-8) return;
        vect2 direction=(end-rim_point)*(1/length);
        if (outside.support)
            outlets[n].push_back({rim_point+direction*std::min(0.02,length*0.25),direction*0.6,edge,forward?1:-1});
        else outlets[n].push_back({rim_point+vect2(side*0.02,0.001),vect2(side*0.6,0),-1,0});
    };
    add(p.outside_left,p.full_floor.front(),-1);
    add(p.outside_right,p.full_floor.back(),1);
}
bool merge_full_pits() {
    for (int a=0;a<(int)basins.size();++a) {
        if (basins[a].capacity<=0 || basins[a].area<basins[a].capacity-1e-8) continue;
        for (const auto& outlet:outlets[a]) {
            int b=outlet.edge<0?-1:edge_basin[outlet.edge];
            if (b<0 || b==a || basins[b].capacity<=0 ||
                basins[b].area<basins[b].capacity-1e-8 ||
                std::abs(basins[a].surface-basins[b].surface)>1e-7) continue;
            const Puddle* left=&basins[a]; const Puddle* right=&basins[b];
            if (left->left.x>right->left.x) std::swap(left,right);
            Puddle merged;
            merged.full_floor=left->full_floor;
            for (vect2 point:right->full_floor)
                if (point.x>merged.full_floor.back().x+1e-8 ||
                    (std::abs(point.x-merged.full_floor.back().x)<1e-8 &&
                     std::abs(point.y-merged.full_floor.back().y)>1e-8)) merged.full_floor.push_back(point);
            merged.outside_left=left->outside_left; merged.outside_right=right->outside_right;
            double rim=std::min(merged.full_floor.front().y,merged.full_floor.back().y);
            for (std::size_t j=1;j<merged.full_floor.size();++j) {
                vect2 u=merged.full_floor[j-1],v=merged.full_floor[j];
                if (u.y>rim && v.y>rim) continue;
                if (u.y>rim) u=u+(v-u)*((rim-u.y)/(v.y-u.y));
                if (v.y>rim) v=v+(u-v)*((rim-v.y)/(u.y-v.y));
                if (merged.floor.empty() || (merged.floor.back()-u).length()>1e-8) merged.floor.push_back(u);
                if ((merged.floor.back()-v).length()>1e-8) merged.floor.push_back(v);
            }
            merged.left=merged.floor.front(); merged.right=merged.floor.back();
            merged.bottom=*std::min_element(merged.floor.begin(),merged.floor.end(),[](vect2 u,vect2 v){return u.y<v.y;});
            merged.bottom_right=merged.bottom;
            merged.capacity=area_below(merged,rim);
            double total=basins[a].area+basins[b].area+basins[a].overflow_area+basins[b].overflow_area;
            merged.area=std::min(total,merged.capacity);
            merged.overflow_area=std::max(0.0,total-merged.capacity);
            update_surface(merged);
            basins[a]=std::move(merged); basins[b]=Puddle{};
            outlets[b].clear();
            for (int& basin:edge_basin) if (basin==b) basin=a;
            rebuild_outlets(a); rebuild_basin_columns();
            return true; // References invalidated; resume with a fresh scan.
        }
    }
    return false;
}
void absorb(int id,int basin) {
    auto& p=basins[basin];
    double total=p.area+DROP_AREA;
    p.area=std::min(p.capacity,total);
    p.overflow_area+=std::max(0.0,total-p.capacity);
    update_surface(p);
    release(id);
}
std::vector<int> nearby_basins(double left, double right) {
    std::vector<int> result;
    for (int x=cell(left); x<=cell(right); ++x) {
        auto it=basin_columns.find(x);
        if (it!=basin_columns.end()) result.insert(result.end(),it->second.begin(),it->second.end());
    }
    std::sort(result.begin(),result.end());
    result.erase(std::unique(result.begin(),result.end()),result.end());
    return result;
}
bool inside_water(const Puddle& p, vect2 point) {
    double height=water_height(p);
    if (p.area<=0 || point.y<p.bottom.y-1e-8 || point.y>height+1e-8) return false;
    auto upper=std::upper_bound(p.floor.begin(),p.floor.end(),point.x,
        [](double x,vect2 vertex){return x<vertex.x;});
    if (upper==p.floor.begin() || upper==p.floor.end()) return false;
    vect2 a=*(upper-1),b=*upper;
    double floor=a.y+(point.x-a.x)*(b.y-a.y)/(b.x-a.x);
    return point.y>=floor-1e-8 && sky(point+vect2(0,1e-7));
}
bool in_water(vect2 point) {
    for (int n:nearby_basins(point.x,point.x)) if (inside_water(basins[n],point)) return true;
    return false;
}
bool collect_water(int id, vect2 destination, double remaining, double travel) {
    auto& d=pool[id];
    int hit=-1; double first=2;
    vect2 delta=destination-d.position;
    for (int n:nearby_basins(std::min(d.position.x,destination.x),std::max(d.position.x,destination.x))) {
        auto& p=basins[n];
        if (p.area<=0) continue;
        double t=2;
        if (inside_water(p,d.position)) t=0;
        else if (delta.y<0) {
            t=(water_height(p)-d.position.y)/delta.y;
            if (t<0 || t>1 || !inside_water(p,d.position+delta*t)) t=2;
        }
        if (t<first) { first=t; hit=n; }
    }
    if (hit<0) return false;
    if (impact_listener && d.edge<0)
        impact_listener(d.position+delta*first,simulation_seconds-(remaining-travel*first)/REAL_SCALE,true);
    absorb(id,hit);
    return true;
}
void settle(int id) {
    auto& d=pool[id];
    // Only bounded basins retain water; unsupported corners discard drops.
    int basin = edge_basin[d.edge];
    if (basin >= 0) {
        absorb(id,basin);
    } else release(id);
}
void advance(int id, double dt) {
    auto& d=pool[id];
    // A bounded number of vertex transitions also handles tiny terrain edges.
    for (int transitions=0; transitions<16 && dt>1e-10; ++transitions) {
        if (d.edge>=0) {
            const auto& e=edges[d.edge];
            int catchment=edge_basin[d.edge];
            // Only nearly flat bottom floors collect dry runoff directly.
            // Dry banks retain visible sliding drops until they reach water
            // or a local minimum. This still bounds particles on huge floors.
            double length=(e.b-e.a).length();
            bool shallow=length>0 && std::abs(e.b.y-e.a.y)/length<=0.02;
            if (catchment>=0 && shallow &&
                d.position.y<=basins[catchment].bottom.y+0.02) {
                absorb(id,catchment); return;
            }
            bool flat=std::abs(e.a.y-e.b.y)<1e-8;
            // Unbounded flats behave as if tilted very slightly right. Keep
            // an explicit spillway direction if this is an overflow particle.
            if (flat && d.flat_direction==0) d.flat_direction=e.b.x>e.a.x?1:-1;
            bool toward_b=flat?d.flat_direction>0:e.b.y<e.a.y;
            vect2 end=toward_b?e.b:e.a;
            vect2 tangent=unit_vector(end-(toward_b?e.a:e.b));
            double speed=std::max(0.6,6.0*std::abs(tangent.y));
            double time=(end-d.position).length()/speed;
            d.velocity=tangent*speed;
            vect2 destination=time>dt?d.position+d.velocity*dt:end;
            if (collect_water(id,destination,dt,std::min(time,dt))) return;
            if (time>dt) {
                d.position=destination;
                return;
            }
            d.position=end; dt-=time;
            int next=toward_b?e.next:e.prev;
            const auto& neighbor=edges[next];
            vect2 other=toward_b?neighbor.b:neighbor.a;
            if (neighbor.support && other.y<end.y-1e-8) { d.edge=next; continue; }
            if (neighbor.support && std::abs(other.y-end.y)<1e-8) {
                d.edge=next;
                if (d.flat_direction!=0) d.flat_direction=toward_b?1:-1;
                continue;
            }
            if (neighbor.support && other.y>=end.y-1e-8) { settle(id); return; }
            // A convex ledge has no upper-facing continuation: release the
            // point just beyond the lip and let gravity take over again.
            d.edge=-1; d.position=d.position+tangent*0.001;
        }
        d.velocity.y=std::max(-20.0,d.velocity.y-25.0*dt);
        vect2 delta=d.velocity*dt;
        double first=1.0; int hit=-1;
        if (++generation==0) { std::fill(visited.begin(),visited.end(),0); ++generation; }
        query(terrain,d.position,d.position+delta,[&](int n) {
            if (visited[n]==generation) return;
            visited[n]=generation;
            const auto& e=edges[n]; vect2 line=e.b-e.a;
            double det=cross(delta,line);
            if (std::abs(det)<1e-14) return;
            double t=cross(e.a-d.position,line)/det;
            double u=cross(e.a-d.position,delta)/det;
            if (t>1e-8 && t<=first && u>=-1e-8 && u<=1.0+1e-8) { first=t; hit=n; }
        });
        if (collect_water(id,d.position+delta*first,dt,dt*first)) return;
        d.position=d.position+delta*first;
        if (hit<0) { if (d.position.y<min_y-10) release(id); return; }
        if (impact_listener) impact_listener(d.position,simulation_seconds-dt*(1-first)/REAL_SCALE,false);
        d.edge=hit; dt*=1-first;
    }
}
}
Settings& settings() { return config; }
void set_impact_listener(ImpactListener listener) { impact_listener=listener; }
const std::vector<Puddle>& puddles() { return basins; }
double water_height(const Puddle& p) {
    return p.surface;
}
void water_span(const Puddle& p,double y,double& left,double& right) {
    left=p.bottom.x; right=p.bottom_right.x;
    bool found=false;
    for (std::size_t i=1;i<p.floor.size();++i) {
        vect2 a=p.floor[i-1],b=p.floor[i];
        if (a.y>y && b.y>y) continue;
        if (a.y>y) a=a+(b-a)*((y-a.y)/(b.y-a.y));
        if (b.y>y) b=b+(a-b)*((y-b.y)/(a.y-b.y));
        if (!found) { left=a.x; right=b.x; found=true; }
        else { left=std::min(left,a.x); right=std::max(right,b.x); }
    }
}
void water_spans(const Puddle& p,double y,std::vector<vect2>& spans) {
    spans.clear();
    if (p.area<=0 || y<p.bottom.y || y>p.surface) return;
    // Intersect the profile's wet intervals with the real level's odd-even
    // sky intervals. Internal ridges, islands and slightly leaning walls
    // remain terrain even when a merged lake spans both sides of them.
    std::vector<double> crossings;
    auto row=sky_rows.find(cell(y));
    if (row==sky_rows.end()) return;
    for (int id:row->second) {
        const auto& e=edges[id];
        if ((e.a.y>y)!=(e.b.y>y))
            crossings.push_back(e.a.x+(y-e.a.y)*(e.b.x-e.a.x)/(e.b.y-e.a.y));
    }
    std::sort(crossings.begin(),crossings.end());
    for (std::size_t i=1;i<p.floor.size();++i) {
        vect2 a=p.floor[i-1],b=p.floor[i];
        if (a.y>y && b.y>y) continue;
        if (a.y>y) a=a+(b-a)*((y-a.y)/(b.y-a.y));
        if (b.y>y) b=b+(a-b)*((y-b.y)/(a.y-b.y));
        for (std::size_t j=1;j<crossings.size();j+=2) {
            double left=std::max(a.x,crossings[j-1]),right=std::min(b.x,crossings[j]);
            if (right<=left) continue;
            if (!spans.empty() && left<=spans.back().y+1e-10)
                spans.back().y=std::max(spans.back().y,right);
            else spans.push_back({left,right});
        }
    }
}
const std::array<Drop,MAX_DROPS>& drops() { return pool; }
int drop_count() { return (int)std::count_if(pool.begin(),pool.end(),[](const Drop& d){return d.active;}); }
double radius(const Drop&) { return 0.035; }
double source_length() { return total_length; }
void clear() {
    bubble_pool={}; breathing_time={}; bubble_cursor=0;
    basins=initial_basins; outlets=initial_outlets; edge_basin=initial_edge_basin;
    next_outlet.assign(basins.size(),0); rebuild_basin_columns();
    pool={}; free_slots.clear(); emission=0; pending_time=0;
    for (auto& p:basins) { p.area=p.overflow_area=0; p.surface=p.bottom.y; }
    std::fill(next_outlet.begin(),next_outlet.end(),0);
    for (int i=MAX_DROPS-1;i>=0;--i) free_slots.push_back(i);
}
bool add_drop(vect2 p) {
    if (free_slots.empty() || !std::isfinite(p.x) || !std::isfinite(p.y)) return false;
    int id=free_slots.back(); free_slots.pop_back(); pool[id]=Drop{};
    pool[id].active=true; pool[id].position=p; return true;
}
void set_scene(const std::vector<std::vector<vect2>>& polygons) {
    clear(); edges.clear(); sources.clear(); terrain.clear(); sky_rows.clear(); total_length=0; min_y=0;
    basins.clear(); basin_columns.clear(); outlets.clear(); next_outlet.clear();
    initial_basins.clear(); initial_outlets.clear(); initial_edge_basin.clear();
    simulation_seconds=0;
    int outer_start=-1, outer_end=-1; double largest=0;
    for (const auto& poly:polygons) {
        if (poly.size()<3) continue;
        int start=(int)edges.size(), count=(int)poly.size(); double area=0;
        for (int i=0;i<count;++i) {
            vect2 a=poly[i], b=poly[(i+1)%count]; area+=cross(a,b);
            min_y=std::min(min_y,a.y);
            edges.push_back({a,b,start+(i+1)%count,start+(i+count-1)%count});
        }
        if (std::abs(area)>largest) { largest=std::abs(area); outer_start=start; outer_end=start+count; }
    }
    visited.assign(edges.size(),0); generation=0;
    edge_basin.assign(edges.size(),-1);
    for (int i=0;i<(int)edges.size();++i) {
        const auto& e=edges[i];
        for (int y=cell(std::min(e.a.y,e.b.y));y<=cell(std::max(e.a.y,e.b.y));++y)
            sky_rows[y].push_back(i);
        // Sample at half-cell intervals and include neighboring cells. This
        // covers every crossed cell without filling a diagonal's huge AABB.
        vect2 delta=e.b-e.a;
        int steps=std::max(1,(int)std::ceil(2*std::max(std::abs(delta.x),std::abs(delta.y))/CELL));
        for (int s=0;s<=steps;++s) {
            vect2 p=e.a+delta*(double(s)/steps);
            for (int x=cell(p.x)-1;x<=cell(p.x)+1;++x)
                for (int y=cell(p.y)-1;y<=cell(p.y)+1;++y) {
                    auto& bucket=terrain[key(x,y)];
                    if (bucket.empty() || bucket.back()!=i) bucket.push_back(i);
                }
        }
    }
    for (int i=0;i<(int)edges.size();++i) {
        auto& e=edges[i]; vect2 mid=(e.a+e.b)*0.5;
        vect2 normal=unit_vector(rotate_90deg(e.b-e.a));
        if (!sky(mid+normal*0.0001)) normal=normal*(-1);
        e.support=normal.y>=-1e-8 ||
                  (normal.y>=-WALL_LEAN_RATIO && std::abs(e.b.x-e.a.x)<=WALL_LEAN_DISTANCE);
        if (i>=outer_start && i<outer_end && std::abs(e.a.x-e.b.x)>1e-8 && sky(mid-vect2(0,0.0001))) {
            total_length+=(e.b-e.a).length(); sources.push_back({i,total_length});
        }
    }
    // Seed each local minimum, then extend both banks along connected
    // upward-facing terrain until their crests. Flat terraces are included.
    // Each pit has its own lower-rim cap; adjacent pits exchange overflow.
    for (int i=0;i<(int)edges.size();++i) {
        const auto& e=edges[i];
        if (!e.support || e.a.y<=e.b.y+1e-8) continue;
        int bank=e.next;
        std::vector<int> path{i};
        while (bank!=i && edges[bank].support && std::abs(edges[bank].b.y-e.b.y)<1e-8) {
            path.push_back(bank); bank=edges[bank].next;
        }
        if (bank==i || !edges[bank].support || edges[bank].b.y<=e.b.y+1e-8) continue;
        path.push_back(bank);
        int first=i,last=bank;
        while (edges[first].prev!=last) {
            int prev=edges[first].prev;
            if (!edges[prev].support || edges[prev].a.y<edges[first].a.y-1e-8) break;
            path.insert(path.begin(),prev); first=prev;
        }
        while (edges[last].next!=first) {
            int next=edges[last].next;
            if (!edges[next].support || edges[next].b.y<edges[last].b.y-1e-8) break;
            path.push_back(next); last=next;
        }
        std::vector<vect2> profile{edges[first].a};
        for (int edge:path) profile.push_back(edges[edge].b);
        if (profile.front().x>profile.back().x) std::reverse(profile.begin(),profile.end());
        bool overhang=false;
        for (std::size_t j=1;j<profile.size();++j) {
            double backtrack=profile[j-1].x-profile[j].x;
            if (backtrack<=1e-8) continue;
            double rise=std::abs(profile[j].y-profile[j-1].y);
            if (backtrack<=WALL_LEAN_DISTANCE && backtrack<=WALL_LEAN_RATIO*rise) {
                profile[j].x=profile[j-1].x;
            } else overhang=true;
        }
        if (overhang) continue; // A height profile cannot represent caves.
        double rim=std::min(profile.front().y,profile.back().y);
        Puddle candidate;
        candidate.full_floor=profile;
        bool forward=edges[first].a.x<=edges[last].b.x;
        candidate.outside_left=forward?edges[first].prev:edges[last].next;
        candidate.outside_right=forward?edges[last].next:edges[first].prev;
        // Clip both banks at the lower rim. Keep every floor vertex below it.
        for (std::size_t j=1;j<profile.size();++j) {
            vect2 a=profile[j-1],b=profile[j];
            if (a.y>rim && b.y>rim) continue;
            if (a.y>rim) a=a+(b-a)*((rim-a.y)/(b.y-a.y));
            if (b.y>rim) b=b+(a-b)*((rim-b.y)/(a.y-b.y));
            if (candidate.floor.empty() || (candidate.floor.back()-a).length()>1e-8)
                candidate.floor.push_back(a);
            if (candidate.floor.empty() || (candidate.floor.back()-b).length()>1e-8)
                candidate.floor.push_back(b);
        }
        if (candidate.floor.size()<3) continue;
        candidate.left=candidate.floor.front(); candidate.right=candidate.floor.back();
        candidate.bottom=*std::min_element(candidate.floor.begin(),candidate.floor.end(),
                                          [](vect2 a,vect2 b){return a.y<b.y;});
        candidate.bottom_right=candidate.bottom;
        for (vect2 p:candidate.floor) if (std::abs(p.y-candidate.bottom.y)<1e-8) {
            if (p.x<candidate.bottom.x) candidate.bottom=p;
            if (p.x>candidate.bottom_right.x) candidate.bottom_right=p;
        }
        candidate.capacity=area_below(candidate,rim);
        candidate.surface=candidate.bottom.y;
        if (candidate.capacity<=1e-10) continue;
        bool obstructed=false;
        double obstruction_height=rim;
        for (int j=0;j<(int)edges.size();++j) {
            if (std::find(path.begin(),path.end(),j)!=path.end()) continue;
            const auto& other=edges[j];
            auto strictly_inside=[&](vect2 point) {
                if (point.y<=candidate.bottom.y+1e-8 || point.y>=rim-1e-8) return false;
                double left,right; water_span(candidate,point.y,left,right);
                return point.x>left+1e-8 && point.x<right-1e-8;
            };
            for (vect2 point:{other.a,other.b}) if (strictly_inside(point)) {
                obstructed=true; obstruction_height=std::min(obstruction_height,point.y);
            }
            for (std::size_t k=0;k<candidate.floor.size();++k) {
                vect2 a=candidate.floor[k],b=candidate.floor[(k+1)%candidate.floor.size()];
                vect2 delta=other.b-other.a,line=b-a;
                double det=cross(delta,line);
                if (std::abs(det)<1e-14) continue;
                double t=cross(a-other.a,line)/det,u=cross(a-other.a,delta)/det;
                if (t>1e-8 && t<1-1e-8 && u>1e-8 && u<1-1e-8) {
                    obstructed=true;
                    obstruction_height=std::min(obstruction_height,(other.a+delta*t).y);
                }
            }
        }
        if (obstructed) {
            // An island high above a broad floor should not disable that floor's
            // puddle entirely. Fill safely up to the first obstruction for now.
            if (obstruction_height<=candidate.bottom.y+1e-8) continue;
            double left,right; water_span(candidate,obstruction_height,left,right);
            candidate.left={left,obstruction_height}; candidate.right={right,obstruction_height};
            candidate.capacity=area_below(candidate,obstruction_height);
            rim=obstruction_height;
        }
        int n=(int)basins.size(); basins.push_back(std::move(candidate));
        vect2 bottom=basins.back().bottom;
        outlets.emplace_back(); next_outlet.push_back(0);
        auto add_outlet=[&](int edge,vect2 rim_point) {
            const auto& outside=edges[edge];
            bool forward=(outside.a-rim_point).length()<1e-7;
            vect2 end=forward?outside.b:outside.a;
            // An uphill continuation is not a physical outlet. Keep excess
            // there until a future model can merge neighboring lakes.
            if (end.y>rim_point.y+1e-8 || (end-rim_point).length()<1e-8) return;
            vect2 direction=unit_vector(end-rim_point);
            double offset=std::min(0.02,(end-rim_point).length()*0.25);
            if (outside.support) {
                outlets.back().push_back({rim_point+direction*offset,direction*0.6,edge,forward?1:-1});
            } else {
                double side=rim_point.x<bottom.x?-1.0:1.0;
                outlets.back().push_back({rim_point+vect2(side*0.02,0.001),vect2(side*0.6,0),-1,0});
            }
        };
        if (std::abs(edges[first].a.y-rim)<1e-8) add_outlet(edges[first].prev,edges[first].a);
        if (std::abs(edges[last].b.y-rim)<1e-8) add_outlet(edges[last].next,edges[last].b);
        for (int edge:path) edge_basin[edge]=n;
        for (int x=cell(basins.back().left.x);x<=cell(basins.back().right.x);++x)
            basin_columns[x].push_back(n);
    }
    initial_basins=basins; initial_outlets=outlets; initial_edge_basin=edge_basin;
}
const std::array<Bubble,128>& bubbles() { return bubble_pool; }
bool fully_submerged(vect2 center,double radius) {
    return in_water(center+vect2(0,radius)) && submerged_fraction(center,radius)>=0.999;
}
void breathe(int player,vect2 head,double radius,bool alive,double dt) {
    if (player<0 || player>=2 || !std::isfinite(dt) || dt<=0) return;
    if (!alive || !fully_submerged(head,radius)) { breathing_time[player]=0; return; }
    breathing_time[player]+=dt/REAL_SCALE;
    if (breathing_time[player]<2.5) return;
    breathing_time[player]=std::fmod(breathing_time[player],2.5);
    for (int i=0;i<3;++i) {
        auto& b=bubble_pool[bubble_cursor++%bubble_pool.size()];
        b={true,head+vect2((i-1)*0.05,radius*0.6),0,double(i+player*3),0.025+i*0.008};
    }
}
double submerged_fraction(vect2 center, double radius) {
    if (radius<=0 || !std::isfinite(radius)) return 0;
    double area=0;
    // Clip circle slices to the complete terrain profile and water surface.
    constexpr int SLICES=32;
    double dx=2*radius/SLICES;
    for (int n:nearby_basins(center.x-radius,center.x+radius)) {
        const auto& p=basins[n];
        double surface=water_height(p);
        if (p.area<=0 || center.y-radius>=surface || center.y+radius<=p.bottom.y) continue;
        for (int i=0;i<SLICES;++i) {
            double x=center.x-radius+(i+0.5)*dx;
            if (x<p.left.x || x>p.right.x) continue;
            auto upper=std::upper_bound(p.floor.begin(),p.floor.end(),x,
                                        [](double value,vect2 point){return value<point.x;});
            if (upper==p.floor.begin() || upper==p.floor.end()) continue;
            vect2 a=*(upper-1),b=*upper;
            double ground=a.y+(x-a.x)*(b.y-a.y)/(b.x-a.x);
            double half=std::sqrt(std::max(0.0,radius*radius-(x-center.x)*(x-center.x)));
            double low=std::max(ground,center.y-half), high=std::min(surface,center.y+half);
            area+=std::max(0.0,high-low)*dx;
        }
    }
    return std::clamp(area/(3.141592653589793*radius*radius),0.0,1.0);
}
static void step(double dt) {
    simulation_seconds+=dt/REAL_SCALE;
    for (auto& b:bubble_pool) if (b.active) {
        double previous=b.age; b.age+=dt/REAL_SCALE;
        b.position.x+=0.03*(std::sin(b.age*6+b.phase)-std::sin(previous*6+b.phase));
        b.position.y+=dt*2.5;
        if (b.age>45 || !in_water(b.position)) b.active=false;
    }
    for (int i=0;i<MAX_DROPS;++i) if (pool[i].active) advance(i,dt);
    while (merge_full_pits()) {} // At most initial pit count - 1 merges per run.
    // Spawn after advancing the entire pool: a recycled slot must not advance
    // twice in one tick or cause a same-tick cascade through several puddles.
    for (int n=0;n<(int)basins.size();++n) {
        auto& p=basins[n];
        while (p.overflow_area+1e-9>=DROP_AREA && !outlets[n].empty() && !free_slots.empty()) {
            const auto& out=outlets[n][next_outlet[n]++%outlets[n].size()];
            int slot=free_slots.back();
            add_drop(out.start);
            pool[slot].edge=out.edge; pool[slot].velocity=out.velocity;
            pool[slot].flat_direction=out.flat_direction;
            p.overflow_area=std::max(0.0,p.overflow_area-DROP_AREA);
        }
    }
    double rate=std::isfinite(config.frequency)?std::clamp(config.frequency,0.0,100.0):0;
    emission+=total_length*rate*dt/REAL_SCALE;
    double whole=std::floor(emission+1e-9);
    int count=(int)std::min(whole,double(MAX_DROPS)); emission=std::max(0.0,emission-whole);
    std::uniform_real_distribution<double> uniform(0,1);
    for (int i=0;i<count && !free_slots.empty() && !sources.empty();++i) {
        double choice=uniform(random)*total_length;
        auto s=std::lower_bound(sources.begin(),sources.end(),choice,[](const Source& a,double b){return a.cumulative<b;});
        const auto& e=edges[s->edge];
        add_drop(e.a+(e.b-e.a)*uniform(random)-vect2(0,0.001));
    }
}
void update(double dt) {
    if (!std::isfinite(dt) || dt<=0) return;
    // Filling puddles does not need the bike's thousands
    // of tiny updates. Fixed 60 Hz keeps cost bounded and independent of FPS;
    // swept contacts still catch thin terrain between these larger steps.
    constexpr double tick=REAL_SCALE/60.0;
    pending_time+=dt;
    while (pending_time+1e-12>=tick) {
        pending_time=std::max(0.0,pending_time-tick);
        step(tick);
    }
}
}

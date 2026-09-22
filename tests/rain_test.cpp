#include "physics/rain.h"
#include "physics/init.h"
#include "main.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <chrono>
#include <algorithm>
void internal_error(const std::string& message, std::source_location) { std::cerr << message; std::abort(); }
void require(bool b,const char* s) { if (!b) { std::cerr<<s<<'\n'; std::exit(1); } }
int count() { int n=0; for (const auto& d:rain::drops()) n+=d.active; return n; }
void run(double time) { for (int i=0;i<int(time/0.005);++i) rain::update(0.005); }
bool near(double a,double b) { return std::abs(a-b)<1e-6; }
const rain::Puddle& pond() {
    const rain::Puddle* result=nullptr;
    for (const auto& p:rain::puddles()) if (p.capacity>0 && (!result || p.bottom.y>result->bottom.y)) result=&p;
    require(result!=nullptr,"expected an active pit"); return *result;
}
int pit_count() {
    int n=0; for(const auto& p:rain::puddles()) n+=p.capacity>0 && p.bottom.y>-9;
    if (n==0) for(const auto& p:rain::puddles()) n+=p.capacity>0;
    return n;
}
void fill(int n) {
    for (int i=0;i<n;++i) require(rain::add_drop({0,0.00001}),"inject water");
    run(0.1);
}
int main() {
    using vects=std::vector<vect2>;
    const vects box={{-10,-10},{10,-10},{10,10},{-10,10}};
    rain::settings().frequency=0;
    rain::set_scene({box});
    require(std::abs(rain::source_length()-20)<1e-8,"only upper boundary emits");
    rain::add_drop({0,0}); run(3);
    require(count()==0 && pit_count()==1 && near(pond().area,rain::DROP_AREA),"flat floor between walls retains water");
    rain::set_scene({box,{{-5,1},{0,0},{5,1},{5,-1},{-5,-1}}});
    rain::add_drop({-4,4}); run(6);
    require(count()==0 && pit_count()==1 && near(pond().area,rain::DROP_AREA),"slide to basin");
    rain::set_scene({box,{{-5,0.01},{5,0},{5,-1},{-5,-1}}});
    rain::add_drop({0,2}); run(3);
    require(rain::drops()[0].position.x>0.5,"shallow slopes have minimum slide speed");
    rain::set_scene({box,{{-5,2},{0,0},{0,-2},{-5,-2}}});
    rain::add_drop({-1,4}); run(5);
    require(rain::drops()[0].position.y<-2,"drop leaves ledge and falls again");
    const vects bowl={{-4,4},{0,0},{4,4},{4,-2},{-4,-2}};
    rain::set_scene({box,bowl});
    require(pit_count()==1 && near(pond().capacity,16),"triangle capacity");
    rain::add_drop({-2,2.001}); run(0.1);
    require(count()==1 && rain::drops()[0].edge>=0 && rain::drops()[0].position.x>-2 &&
            near(pond().area,0),"dry bank runoff stays visible and slides instead of filling remotely");
    run(2);
    require(count()==0 && near(pond().area,0.01),"bank runoff eventually reaches the puddle");
    rain::clear();
    fill(400);
    require(near(pond().area,4) && near(rain::water_height(pond()),2),"quarter volume means half height");
    rain::add_drop({0,5}); run(0.6);
    require(near(pond().area,4.01),"water surface absorbs falling drops");
    fill(2000);
    require(near(pond().area,16) && near(rain::water_height(pond()),4),"capacity clamps during overflow");
    require(count()==801,"each excess drop reappears once at a rim");
    int left_spills=0,right_spills=0;
    for (const auto& d:rain::drops()) if(d.active) { left_spills+=d.position.x<0; right_spills+=d.position.x>0; }
    require(std::abs(left_spills-right_spills)<=1,"equal rims alternate overflow");
    require(rain::submerged_fraction({0,2},0.25)>0.99,"fully submerged wheel");
    require(std::abs(rain::submerged_fraction({0,4},0.25)-0.5)<0.01,"half submerged wheel");
    require(near(rain::submerged_fraction({3,0.5},0.25),0),"bank excludes underground wheels");
    require(near(rain::submerged_fraction({0,5},0.25),0),"above water is dry");
    motorst motor{};
    for (auto* body:{&motor.bike,&motor.left_wheel,&motor.right_wheel}) {
        body->r={0,2}; body->radius=0.25; body->v={10,-10}; body->angular_velocity=12;
    }
    motor.body_r={0,2}; motor.body_v={4,-4};
    motor.volting_left=true; motor.angular_velocity_pre_left_volt=0; motor.left_volt_water_scale=1;
    auto heavy=motor;
    rain::apply_water_drag(motor,0.1);
    rain::settings().water_resistance=1;
    rain::apply_water_drag(heavy,0.1);
    require(motor.left_wheel.v.x>heavy.left_wheel.v.x && motor.bike.angular_velocity>heavy.bike.angular_velocity,
            "new default water resistance is gentler than original");
    rain::settings().water_resistance=0;
    auto no_drag=motor;
    rain::apply_water_drag(no_drag,1);
    require(near(no_drag.left_wheel.v.x,motor.left_wheel.v.x) && near(no_drag.bike.angular_velocity,motor.bike.angular_velocity),
            "water resistance can be disabled");
    rain::settings().water_resistance=0.35;
    require(motor.left_wheel.v.x>0 && motor.left_wheel.v.x<10 &&
            near(motor.left_wheel.v.x,-motor.left_wheel.v.y),"drag slows all directions without reversing");
    require(motor.bike.angular_velocity<12 && near(motor.bike.angular_velocity,12*motor.left_volt_water_scale),"volt tracks remaining boost");
    auto dry=motor; dry.bike.r={0,8};
    double spin=dry.bike.angular_velocity, vx=dry.bike.v.x;
    rain::apply_water_drag(dry,0.1);
    require(near(dry.bike.angular_velocity,spin) && near(dry.bike.v.x,vx),"dry chassis unchanged");
    rain::clear();
    require(near(pond().area,0) && near(rain::submerged_fraction({0,2},0.25),0),"clear removes water and drag");
    rain::set_scene({box,{{-4,4},{0,0},{2,2},{2,-2},{-4,-2}}});
    fill(1000);
    require(near(pond().capacity,4) && near(rain::water_height(pond()),2),"lower asymmetric rim limits fill");
    require(count()==600,"overflow conserves excess drop count");
    for (const auto& d:rain::drops()) if(d.active)
        require(d.position.x>=2 && d.position.y<2,"spill follows outside edge at lower right rim");
    rain::set_scene({box,{{-4,4},{0,0},{2,2},{4,2},{4,-2},{-4,-2}}});
    fill(403);
    require(count()==3,"flat spillway retains overflow drops");
    for (const auto& d:rain::drops()) if(d.active)
        require(d.position.x>=4 && d.position.y<2,"overflow drains beyond flat rim terrace");
    rain::set_scene({box,{{-4,4},{0,0},{2.0025,2},{2.0025,-2},{-4,-2}}});
    fill(401);
    require(count()==0 && near(pond().overflow_area,0.0075),"fractional excess retained until one whole drop");
    fill(1);
    require(count()==1 && near(pond().overflow_area,0.0075),"fractional overflow conserves volume");
    rain::set_scene({box,bowl,{{-0.2,1},{0.2,1},{0,1.5}}});
    require(near(pond().left.y,1),"island limits fill instead of disabling the entire basin");
    const vects trough={{-4,2},{-2,0},{2,0},{4,2},{4,-2},{-4,-2}};
    rain::set_scene({box,trough});
    require(pit_count()==1 && near(pond().capacity,12),"three-edge trough capacity");
    fill(600);
    double height=rain::water_height(pond());
    require(near(4*height+height*height,6),"trapezoid volume determines surface height");
    double span_left,span_right;
    rain::water_span(pond(),0,span_left,span_right);
    require(near(span_left,-2) && near(span_right,2),"renderer spans full flat floor");
    require(std::abs(rain::submerged_fraction({0,height},0.25)-0.5)<0.01,"flat-bottom water applies partial wheel drag");
    fill(601);
    require(near(pond().area,12) && count()==1,"flat-bottom trough overflows");
    vects reverse_trough=trough;
    std::reverse(reverse_trough.begin(),reverse_trough.end());
    rain::set_scene({box,reverse_trough});
    require(pit_count()==1 && near(pond().capacity,12),"trough detection independent of polygon winding");
    rain::set_scene({box,{{-4,2},{-2,0},{0,0},{2,0},{4,2},{4,-2},{-4,-2}}});
    require(pit_count()==1 && near(pond().capacity,12),"split collinear floor forms one puddle");
    const vects uneven_bucket={{-5,3},{-4,1},{-3,0.08},{0,0.03},{3,0},{4,1},{5,3},{5,-2},{-5,-2}};
    rain::set_scene({box,uneven_bucket});
    require(pit_count()==1 && near(pond().capacity,24.75),"whole multi-segment bucket detected despite slightly tilted floor");
    for (int i=0;i<100;++i) rain::add_drop({3,0.00001});
    run(0.2);
    require(near(pond().area,1) && rain::water_height(pond())>0.08,"water rises across uneven floor instead of stopping at first short edge");
    rain::water_span(pond(),rain::water_height(pond()),span_left,span_right);
    require(span_left<-3 && span_right>3,"multi-edge waterline spans bucket");
    vects reversed_bucket=uneven_bucket;
    std::reverse(reversed_bucket.begin(),reversed_bucket.end());
    rain::set_scene({box,reversed_bucket});
    require(pit_count()==1 && near(pond().capacity,24.75),"multi-edge pit independent of winding");
    rain::set_scene({box,{{-5,3},{-3,0},{0,1},{3,0},{5,3},{5,-2},{-5,-2}}});
    require(pit_count()==2,"separate neighboring pits detected at internal crest");
    for (int i=0;i<260;++i) rain::add_drop({-3,0.00001});
    run(2);
    double stored=0; int wet_pits=0;
    for (const auto& p:rain::puddles()) { stored+=p.area+p.overflow_area; wet_pits+=p.area>0; }
    require(wet_pits==2 && near(stored+count()*rain::DROP_AREA,2.6),"neighboring pit receives overflow without losing water");
    for (int i=0;i<600;++i) rain::add_drop({-3,0.00001});
    run(2);
    require(pit_count()==1 && pond().surface>1,"jagged bucket merges above its interior ridge");
    std::vector<vect2> wet_spans;
    rain::water_spans(pond(),0.5,wet_spans);
    require(wet_spans.size()==2 && wet_spans[0].y<0 && wet_spans[1].x>0,
            "merged lake drawing leaves its submerged central ridge uncovered");
    rain::water_spans(pond(),1.01,wet_spans);
    require(wet_spans.size()==1,"water above the ridge forms one continuous span");
    stored=0;
    for (const auto& p:rain::puddles()) stored+=p.area+p.overflow_area;
    require(near(stored+count()*rain::DROP_AREA,8.6),"lake merge conserves water");
    require(rain::fully_submerged({0,pond().surface-0.3},0.2),"whole head below lake surface");
    require(!rain::fully_submerged({0,pond().surface-0.1},0.2),"partly exposed head does not get breathing ring");
    const vect2 head={0,pond().surface-0.3};
    constexpr double real_scale=1000.0*STOPWATCH_MULTIPLIER*STOPWATCH_TO_PHYS_TIME;
    rain::breathe(0,head,0.2,true,real_scale*2.6);
    require(std::count_if(rain::bubbles().begin(),rain::bubbles().end(),[](const auto& b){return b.active;})==3,"submerged rider exhales a small bubble group");
    double bubble_y=rain::bubbles()[0].position.y;
    run(0.025);
    require(rain::bubbles()[0].position.y>bubble_y,"bubbles rise");
    run(1);
    require(std::none_of(rain::bubbles().begin(),rain::bubbles().end(),[](const auto& b){return b.active;}),"bubbles disappear at surface");
    rain::clear();
    require(pit_count()==2,"clearing water restores separate empty pits");
    // Warm Up-shaped chain: the short wall near the left end leans inward
    // by 7 mm. It must not trap all overflow below that wall's lower vertex.
    vects stepped_floor={{18.642,15.044},{18.600,3.680},{16.880,1.440},
        {13.810,-0.104},{8.146,1.023},{4.772,-0.053},{1.628,0.789},
        {-0.919,0.002},{-3.811,-0.009},{-6.015,0.602},{-7.360,0.605},
        {-9.304,0.098},{-11.136,0.247},{-11.129,0.726},
        {-12.377,0.690},{-12.400,15.425}};
    for (int winding=0;winding<2;++winding) {
        rain::set_scene({stepped_floor});
        for (int i=0;i<5000;++i) rain::add_drop({13.810,-0.10399});
        run(30);
        int lakes=0; double volume=0, surface=0;
        for (const auto& p:rain::puddles()) {
            lakes+=p.capacity>0; volume+=p.area+p.overflow_area;
            if (p.area>0) surface=std::max(surface,p.surface);
        }
        require(lakes==1 && surface>1.1,"stepped pits merge past a slightly leaning wall in either winding");
        require(near(volume+count()*rain::DROP_AREA,50),"stepped overflow conserves all supplied water");
        std::reverse(stepped_floor.begin(),stepped_floor.end());
    }
    rain::set_scene({{{-1000,-10},{1000,-10},{1000,20},{-1000,20}}});
    for (int i=0;i<10000;++i) rain::add_drop({-999+(i%1000)*1.998,-9.99999});
    run(0.05);
    require(count()==0 && near(pond().area,100),"wide flat pit immediately absorbs 10k grounded drops");
    require(near(pond().surface,-9.95),"wide puddle stores volume as a thin water layer");
    rain::set_scene({box}); fill(100); run(3);
    require(near(rain::water_height(pond()),-9.95),"vertical-wall rectangle has linear fill height");
    rain::set_scene({box,{{-2,0},{2,0},{2,-1},{-2,-1}}});
    require(std::all_of(rain::puddles().begin(),rain::puddles().end(),[](const auto& p){return p.bottom.y<-9;}),"open platform does not retain puddle");
    rain::add_drop({0,1}); run(1);
    require(rain::drops()[0].active && rain::drops()[0].position.x>0.1 && near(rain::drops()[0].position.y,0),"ordinary rain drifts right on flat platform");
    run(6);
    require(rain::drops()[0].position.x>=2 && rain::drops()[0].position.y<-1,"flat platform drains over its right edge");
    rain::set_scene({box});
    rain::settings().frequency=1;
    constexpr double scale=1000.0*STOPWATCH_MULTIPLIER*STOPWATCH_TO_PHYS_TIME;
    rain::update(scale);
    require(count()==20,"frequency measured per metre per real second");
    rain::clear(); rain::settings().frequency=0;
    for(int i=0;i<rain::MAX_DROPS;++i) require(rain::add_drop({(i%100)*0.1-5,5+(i/100)*0.01}),"fill pool");
    require(!rain::add_drop({0,0}) && rain::drop_count()==rain::MAX_DROPS,"bounded pool and HUD count");
    auto start=std::chrono::steady_clock::now(); run(1);
    std::cout<<rain::MAX_DROPS<<" drops, 200 updates: "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<"s\n";
    rain::clear(); require(count()==0 && rain::source_length()==20,"clear retains scene");
    std::cout<<"PASS rain, triangular fill, capacity, submersion, drag, volt bookkeeping, counts\n";
}

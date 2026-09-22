#include "physics/rain.h"
#include "pic/pic8.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <climits>

static void water_ring(pic8& pic,vect2 center,int radius,unsigned char white) {
    int cx=(int)center.x,cy=(int)center.y;
    if (cx+radius<0 || cx-radius>=pic.get_width() || cy+radius<0 || cy-radius>=pic.get_height()) return;
    for (int y=-radius;y<=radius;++y) {
        int x=(int)std::round(std::sqrt(double(radius*radius-y*y)));
        pic.ppixel(cx-x,cy+y,white); pic.ppixel(cx+x,cy+y,white);
        pic.ppixel(cx+y,cy-x,white); pic.ppixel(cx+y,cy+x,white);
    }
}
void rain::render_breathing(pic8& pic,vect2 head,double radius,vect2 bottomleft,double scale,unsigned char white) {
    vect2 center=(head-bottomleft)*scale;
    int pixels=std::max(3,(int)((radius+0.07)*scale));
    water_ring(pic,center,pixels,white);
    water_ring(pic,center,pixels+1,white);
}

void rain::render(pic8& pic, vect2 bottomleft, double scale, const unsigned char* palette) {
    if (!palette) return;
    static std::array<unsigned char,768> cached{};
    static std::array<unsigned char,256> blend{};
    static bool ready=false;
    if (!ready || !std::equal(cached.begin(),cached.end(),palette)) {
        std::copy(palette,palette+768,cached.begin()); ready=true;
        // The game uses an indexed palette. Precompute its closest available
        // colour to a 50% light grey-blue overlay for each background colour.
        constexpr int RAIN_RED = 195, RAIN_GREEN = 205, RAIN_BLUE = 215;
        for (int i=0;i<256;++i) {
            int r=(palette[3*i]+RAIN_RED)/2;
            int g=(palette[3*i+1]+RAIN_GREEN)/2;
            int b=(palette[3*i+2]+RAIN_BLUE)/2;
            int best=INT_MAX;
            for (int j=0;j<256;++j) {
                int dr=palette[3*j]-r,dg=palette[3*j+1]-g,db=palette[3*j+2]-b;
                int distance=dr*dr+dg*dg+db*db;
                if (distance<best) { best=distance; blend[i]=(unsigned char)j; }
            }
        }
    }
    std::vector<vect2> spans;
    for (const auto& water:puddles()) {
        if (water.area<=0) continue;
        double surface=water_height(water);
        vect2 bottom=(water.bottom-bottomleft)*scale;
        double top=(surface-bottomleft.y)*scale;
        int y0=(int)std::max(0.0,std::ceil(bottom.y));
        int y1=(int)std::min(double(pic.get_height()-1),std::floor(top));
        for (int y=y0;y<=y1;++y) {
            water_spans(water,bottomleft.y+y/scale,spans);
            auto* row=pic.get_row(y);
            for (vect2 span:spans) {
                int x0=(int)std::max(0.0,std::ceil((span.x-bottomleft.x)*scale));
                int x1=(int)std::min(double(pic.get_width()-1),std::floor((span.y-bottomleft.x)*scale));
                for (int x=x0;x<=x1;++x) row[x]=blend[row[x]];
            }
        }
    }
    for (const auto& d:drops()) {
        if (!d.active) continue;
        vect2 p=(d.position-bottomleft)*scale;
        double rx=std::max(1.0,radius(d)*scale);
        double ry=std::max(2.0,rx*2);
        if (p.x+rx<0 || p.x-rx>=pic.get_width() || p.y+2*ry<0 || p.y>=pic.get_height()) continue;
        int x0=std::max(0,(int)std::floor(p.x-rx)), x1=std::min(pic.get_width()-1,(int)std::ceil(p.x+rx));
        int y0=std::max(0,(int)std::floor(p.y)), y1=std::min(pic.get_height()-1,(int)std::ceil(p.y+2*ry));
        for (int y=y0;y<=y1;++y) {
            auto* row=pic.get_row(y);
            for (int x=x0;x<=x1;++x) {
                double dx=(x-p.x)/rx,dy=(y-p.y-ry)/ry;
                if (dx*dx+dy*dy<=1) row[x]=blend[row[x]];
            }
        }
    }
    unsigned char white=0; int brightest=-1;
    for (int i=0;i<256;++i) {
        int brightness=palette[i*3]+palette[i*3+1]+palette[i*3+2];
        if (brightness>brightest) { brightest=brightness; white=(unsigned char)i; }
    }
    for (const auto& b:bubbles()) if (b.active)
        water_ring(pic,(b.position-bottomleft)*scale,std::max(1,(int)(b.radius*scale)),white);
}

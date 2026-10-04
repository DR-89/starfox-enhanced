// Native PICA/texture/depth/slider check only. This is not the game entry point.
#include "native_display.hpp"
#include "native_gpu.hpp"
#include "pica_scene_shader.hpp"

namespace {
using namespace starfox::platform::nintendo_3ds;
void quad(std::vector<PicaVertex>& vertices,const std::array<Point3,4>& points,
    std::array<float,4> colour={1,1,1,1}) {
    constexpr std::array<std::array<float,2>,4> uv{{{0,0},{1,0},{1,1},{0,1}}};
    for(unsigned corner:{0U,1U,2U,0U,2U,3U}) vertices.push_back({points[corner],colour,uv[corner]});
}
void cube(std::vector<PicaVertex>& vertices) {
    constexpr float s=48;
    const std::array<Point3,8> points{{{-s,-s,-s},{s,-s,-s},{s,s,-s},{-s,s,-s},
        {-s,-s,s},{s,-s,s},{s,s,s},{-s,s,s}}};
    constexpr unsigned faces[][4]={{0,1,2,3},{4,7,6,5},{0,4,5,1},{3,2,6,7},{0,3,7,4},{1,5,6,2}};
    constexpr std::array<float,4> colours[]={{1,1,1,1},{.8F,.8F,.8F,1},{.5F,.7F,1,1},
        {1,.8F,.6F,1},{.7F,1,.8F,1},{1,.6F,.8F,1}};
    for(unsigned face=0;face<6;++face)
        quad(vertices,{points[faces[face][0]],points[faces[face][1]],points[faces[face][2]],points[faces[face][3]]},colours[face]);
}
int diagnostic(NativeDisplay& display) {
    NativeGpu gpu(pica_scene_shader); // Destroy/sync the GPU BEFORE gfxExit.
    Canvas caption(top_width);CockpitDashboard lower;
    std::array<std::uint8_t,8*8*4> checker{};
    for(unsigned y=0;y<8;++y) for(unsigned x=0;x<8;++x) {
        const unsigned at=(y*8+x)*4;
        const bool bright=((x/2+y/2)%2)==0;
        checker[at]=bright?240:35;checker[at+1]=bright?184:82;checker[at+2]=bright?78:165;checker[at+3]=255;
    }
    std::vector<PicaVertex> vertices;
    quad(vertices,{{{0,0,0},{float(top_width),0,0},{float(top_width),float(screen_height),0},{0,float(screen_height),0}}});
    cube(vertices);cube(vertices);
    // A translucent primitive submitted after opaque geometry tests the same
    // pass/alpha/depth state used by the game adapter; no copied-eye image.
    quad(vertices,{{{-75,-60,450},{75,-60,450},{75,60,450},{-75,60,450}}},{.2F,.8F,1,.35F});
    std::array<PicaDraw,4> draws{{
        {0,6,0,pica_identity,PicaSpace::screen,false,false,false},
        {6,36,1},{42,36,1},{78,6,pica_no_texture,pica_identity,PicaSpace::world,true,false,true}}};
    bool setup=true,caption_dirty=true;float x{},y{};
    starfox::input::ButtonMask previous{};
    StereoSettings settings;settings.near_plane=16;
    while(true) {
        const auto input=display.poll();if(!input.running) return 0;
        if((input.held&(starfox::input::select|starfox::input::start))==(starfox::input::select|starfox::input::start)) return 0;
        if((input.held&starfox::input::a) && !(previous&starfox::input::a)) {setup=false;caption_dirty=true;}
        if((input.held&starfox::input::b) && !(previous&starfox::input::b)) {setup=true;caption_dirty=true;}
        previous=input.held;
        if(caption_dirty) {
            caption.clear({8,15,28});
            caption.text(12,12,"PICA200 GPU CHECK / NOT THE GAME",{183,224,240});
            if(setup) caption.text(24,56,"A: TEXTURES / DEPTH / 3D SLIDER\n\nB: RETURN TO THIS PAGE\nCIRCLE PAD: MOVE FRONT CUBE\n\nSELECT + START: EXIT\n\nREAL PRE-GAME MENU IS RETAINED\nIN THE SEPARATE GAME PORT",{227,235,242});
            else caption.text(16,221,"SLIDER: DEPTH / B: BACK / SELECT+START: EXIT",{213,237,244});
            caption_dirty=false;
        }
        if(!setup) {
            x=std::clamp(x+2*((input.held&starfox::input::right)!=0)-2*((input.held&starfox::input::left)!=0),-160.F,160.F);
            y=std::clamp(y+2*((input.held&starfox::input::up)!=0)-2*((input.held&starfox::input::down)!=0),-96.F,96.F);
        }
        draws[1].model[0][3]=x;draws[1].model[1][3]=y;draws[1].model[2][3]=300;
        draws[2].model[0][3]=32;draws[2].model[1][3]=24;draws[2].model[2][3]=650;
        const auto top=caption.view();
        const std::array<PicaImage,2> images{{{top.pixels,top.width,top.height,top.pitch,3},
            {checker,8,8,32,4,true}}};
        const auto plan=plan_frame(input.slider,input.stereoscopic_hardware,setup?ScreenUse::setup:ScreenUse::world,settings);
        PicaFrame frame{plan,std::span(vertices).first(setup?6:vertices.size()),
            std::span(draws).first(setup?1:draws.size()),images};
        HudState hud;hud.lives=2;hud.bombs=3;hud.shield_percent=76;hud.boost_percent=92;
        hud.ally_percent={84,58,95};hud.radio_message="SYNTHETIC GPU CHECK / NO GAME DATA";
        lower.update(hud);gpu.present(frame,lower.view());
    }
}
}
int main() {
    NativeDisplay display;
    try {return diagnostic(display);}
    catch(const std::exception& error) {
        // The failed presenter has already finalized before CPU LCD output.
        Canvas screen(top_width),lower(bottom_width);
        screen.clear({17,25,38});screen.text(16,20,"PICA200 STARTUP / RENDER ERROR",{239,90,99});
        screen.text(16,48,error.what(),{227,235,242},1,368,156);
        screen.text(16,220,"SELECT + START: EXIT",{183,224,240});
        lower.clear({17,25,38});lower.text(12,24,"NATIVE GPU CHECK FAILED\n\nTHIS IS NOT A GAME BUILD",{227,235,242});
        const auto mono=plan_frame(0,false,ScreenUse::setup);
        while(true) {
            const auto input=display.poll();if(!input.running) break;
            if((input.held&(starfox::input::select|starfox::input::start))==(starfox::input::select|starfox::input::start)) break;
            display.present(mono,screen.view(),{},lower.view());
        }
        return 1;
    }
}

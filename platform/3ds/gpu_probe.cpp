// Native PICA/texture/depth/slider check only. This is not the game entry point.
#include "native_display.hpp"
#include "native_gpu.hpp"
#include "starfox/platform/nintendo_3ds/pica_shapes.hpp"
#include "starfox/platform/nintendo_3ds/pica_raster.hpp"
#include "starfox/platform/nintendo_3ds/pica_composite.hpp"
#include "pica_scene_shader.hpp"

namespace {
using namespace starfox::platform::nintendo_3ds;
void quad(std::vector<PicaVertex>& vertices,const std::array<Point3,4>& points,
    std::array<float,4> colour={1,1,1,1}) {
    constexpr std::array<std::array<float,2>,4> uv{{{0,0},{1,0},{1,1},{0,1}}};
    for(unsigned corner:{0U,1U,2U,0U,2U,3U}) vertices.push_back({points[corner],colour,uv[corner]});
}
starfox::assets::Shape source_cube() {
    constexpr int s=48;
    starfox::assets::Shape shape;shape.vertices={{-s,-s,-s},{s,-s,-s},{s,s,-s},{-s,s,-s},
        {-s,-s,s},{s,-s,s},{s,s,s},{-s,s,s}};
    constexpr unsigned faces[][4]={{0,1,2,3},{4,7,6,5},{0,4,5,1},{3,2,6,7},{0,3,7,4},{1,5,6,2}};
    shape.colour_words={0x4001,0xc00b,0x0053,0xc004,0xc009,0xc008};
    for(unsigned face=0;face<6;++face) {
        starfox::assets::Face source;source.visibility_index=-1;source.colour_id=face;
        for(auto vertex:faces[face]) source.vertex_indices.push_back(vertex);
        shape.faces.push_back(source);
    }
    starfox::assets::TextureImage art;art.descriptor=0x4001;art.u_mask=art.v_mask=7;
    art.coordinates={{{0,0},{8,0},{8,8},{0,8}}};art.texels.resize(64);
    for(unsigned y=0;y<8;++y) for(unsigned x=0;x<8;++x) art.texels[y*8+x]=((x/2+y/2)&1)?5:15;
    shape.textures.push_back(std::move(art));return shape;
}
int diagnostic(NativeDisplay& display) {
    NativeGpu gpu(pica_scene_shader); // Destroy/sync the GPU BEFORE gfxExit.
    Canvas caption(top_width);CockpitDashboard lower;
    const auto shape=source_cube();
    const std::array<starfox::render::Rgba8,16> colours{{{0,0,0},{48,48,60},{112,28,32},{38,70,150},
        {188,72,24},{24,130,132},{82,20,26},{35,52,120},{118,46,132},{38,110,52},
        {82,82,92},{118,118,128},{156,156,166},{202,202,210},{238,238,242},{255,255,255}}};
    starfox::render::SoftwareRenderer source_renderer;PicaShapes source_geometry;
    PicaRaster source_raster;PicaComposite compositor;
    auto sky=std::make_shared<starfox::simulation::SnesPpuState>();
    sky->main_screen=2;sky->bg2_screen_size=0;
    sky->bg2_character_base=0x4000;sky->bg2_screen_base=0x6000;
    sky->cgram[1]=uint16_t(7|(17<<5)|(28<<10));sky->cgram[2]=uint16_t(25|(28<<5)|(31<<10));
    for(unsigned row=0;row<8;++row) {
        sky->vram[0x8000+row*2]=(row==4)?0:255;
        sky->vram[0x8000+row*2+1]=(row==4)?255:0;
    }
    // Synthetic SNES tiles, not cartridge assets or a gameplay backdrop.
    PpuBatch sky_batch{{{PpuLayer::bg2}},PicaSpace::scenery,true,0,208};
    std::vector<PicaVertex> vertices;
    std::vector<PicaDraw> draws;std::vector<PicaImage> images;
    std::vector<PicaVertex> alpha_vertices;std::vector<PicaDraw> alpha_draws;
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
            if(setup) caption.text(24,56,"A: SOURCE MODELS / DEPTH / SLIDER\n\nB: RETURN TO THIS PAGE\nCIRCLE PAD: MOVE FRONT CUBE\n\nSELECT + START: EXIT\n\nREAL PRE-GAME MENU IS RETAINED\nIN THE SEPARATE GAME PORT",{227,235,242});
            else caption.text(16,221,"SLIDER: DEPTH / B: BACK / SELECT+START: EXIT",{213,237,244});
            caption_dirty=false;
        }
        if(!setup) {
            x=std::clamp(x+2*((input.held&starfox::input::right)!=0)-2*((input.held&starfox::input::left)!=0),-160.F,160.F);
            y=std::clamp(y+2*((input.held&starfox::input::up)!=0)-2*((input.held&starfox::input::down)!=0),-96.F,96.F);
        }
        const auto top=caption.view();
        const auto plan=plan_frame(input.slider,input.stereoscopic_hardware,setup?ScreenUse::setup:ScreenUse::world,settings);
        vertices.clear();draws.clear();images.clear();source_geometry.clear();
        quad(vertices,{{{0,0,0},{float(top_width),0,0},{float(top_width),float(screen_height),0},{0,float(screen_height),0}}});
        draws.push_back({0,6,0,pica_identity,PicaSpace::screen,false,false,false});
        images.push_back({top.pixels,top.width,top.height,top.pitch,3});
        std::vector<PicaFrame> groups{{plan,vertices,draws,images}};
        if(!setup) {
            groups.push_back(source_raster.prepare(sky,sky_batch,plan));
            starfox::render::RenderPose pose;pose.vanish_x=128;pose.vanish_y=112;
            pose.x=x;pose.y=-y;pose.z=300;pose.continuous_geometry=true;
            source_geometry.append(source_renderer.prepare_primitives(shape,pose),colours);
            pose.x=32;pose.y=-24;pose.z=650;
            source_geometry.append(source_renderer.prepare_primitives(shape,pose),colours);
            const auto converted=source_geometry.frame(plan);
            groups.push_back(converted);
            // The separate translucent pass still exercises depth/alpha state.
            alpha_vertices.clear();alpha_draws.clear();
            quad(alpha_vertices,{{{-75,-60,450},{75,-60,450},{75,60,450},{-75,60,450}}},{.2F,.8F,1,.35F});
            alpha_draws.push_back({0,6,pica_no_texture,pica_identity,PicaSpace::world,true,false,true});
            groups.push_back({plan,alpha_vertices,alpha_draws,{}});
        }
        HudState hud;hud.lives=2;hud.bombs=3;hud.shield_percent=76;hud.boost_percent=92;
        hud.ally_percent={84,58,95};hud.radio_message="SYNTHETIC GPU CHECK / NO GAME DATA";
        lower.update(hud);
        const auto frame=compositor.prepare(plan,groups,lower.view());gpu.present(frame,lower.view());
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

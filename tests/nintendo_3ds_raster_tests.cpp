#include "starfox/platform/nintendo_3ds/pica_raster.hpp"
#include "starfox/platform/nintendo_3ds/pica_composite.hpp"
#include "starfox/platform/nintendo_3ds/pica_window.hpp"
#include "starfox/platform/nintendo_3ds/pica_colour.hpp"
#include <iostream>

namespace {
using namespace starfox;
using namespace platform::nintendo_3ds;
unsigned checks{};
void require(bool value,const char* why) {++checks;if(!value) throw std::runtime_error(why);}
template<class Action> void rejected(Action action,const char* why) {
    bool failed=false;try {action();} catch(const std::exception&) {failed=true;} require(failed,why);
}
void tile(simulation::SnesPpuState& ppu,unsigned character_base,unsigned number,unsigned ink,unsigned depth=4) {
    for(unsigned y=0;y<8;++y) for(unsigned bit=0;bit<depth;++bit)
        ppu.vram[(character_base*2+number*depth*8+(bit/2)*16+y*2+(bit&1))&65535]=(ink&(1U<<bit))?255:0;
}
void object(simulation::SnesPpuState& ppu,unsigned i,unsigned x,unsigned y,unsigned number) {
    ppu.oam[i*4]=x;ppu.oam[i*4+1]=y;ppu.oam[i*4+2]=number;ppu.oam[i*4+3]=0x20;
    tile(ppu,0,number,1);
}
std::shared_ptr<simulation::SnesPpuState> source() {
    auto ppu=std::make_shared<simulation::SnesPpuState>();
    ppu->main_screen=23;ppu->object_select=0;
    ppu->bg1_screen_base=0x6400;ppu->bg1_character_base=0x4400;ppu->bg1_screen_size=0;
    ppu->bg2_screen_base=0x6000;ppu->bg2_character_base=0x4000;ppu->bg2_screen_size=0;
    ppu->bg3_screen_base=0x6800;ppu->bg3_character_base=0x4800;ppu->bg3_screen_size=0;
    for(unsigned i=0;i<1024;++i) {
        ppu->vram[0xc000+i*2+1]=4; // palette 1, tile 0, low priority.
        ppu->vram[0xc800+i*2]=1;ppu->vram[0xc800+i*2+1]=32; // high BG1.
    }
    tile(*ppu,0x4000,0,1);tile(*ppu,0x4400,1,2);tile(*ppu,0x4800,0,3,2);
    ppu->cgram[17]=0; // Opaque black, not transparent index zero.
    ppu->cgram[2]=31<<5;ppu->cgram[3]=31<<10;ppu->cgram[129]=31;
    for(unsigned i=0;i<128;++i) ppu->oam[i*4+1]=240;
    object(*ppu,0,10,10,189);object(*ppu,1,100,180,0x61);object(*ppu,2,190,85,7);
    return ppu;
}
std::array<unsigned,4> pixel(const PicaImage& image,unsigned x,unsigned y) {
    const auto offset=std::size_t(y)*image.pitch+x*4;
    return {image.pixels[offset],image.pixels[offset+1],image.pixels[offset+2],image.pixels[offset+3]};
}
void raster() {
    const auto plan=plan_frame(1,true,ScreenUse::world);
    auto ppu=source();const auto original=*ppu;
    PicaRaster renderer;PpuBatch batch;batch.space=PicaSpace::scenery;batch.expand_horizontal=true;
    batch.passes.push_back({PpuLayer::bg2});
    auto frame=renderer.prepare(ppu,batch,plan);
    require(frame.vertices.size()==6 && frame.draws.size()==1 && frame.textures.size()==1,"Native sky quad missing");
    require(frame.draws[0].space==PicaSpace::scenery && !frame.draws[0].depth_test && frame.draws[0].alpha_blend,"Sky placed at model/HUD depth");
    const auto image=frame.textures[0];
    const auto* ownership=image.source_layers.data();
    require(image.width==464 && image.height==240 && frame.vertices[0].position[0]==-32,"Infinite sky guard/layout mismatch");
    for(unsigned y=0;y<240;++y) for(unsigned x=0;x<464;++x)
        require(pixel(image,x,y)==std::array<unsigned,4>{0,0,0,255},"Opaque black source ink became transparent or viewport guard was uncovered");
    for(float slider:{0.F,.25F,.5F,1.F}) {
        const auto other=renderer.prepare(ppu,batch,plan_frame(slider,true,ScreenUse::world));
        require(other.textures[0].pixels.data()==image.pixels.data(),"Eye/slider read replaced cached sky artwork");
    }
    require(renderer.work().decodes==1 && renderer.work().colour_updates==1,"Slider rerasterized/recoloured the 2D source");
    auto moved=std::make_shared<simulation::SnesPpuState>(*ppu);moved->oam[0]=20;
    renderer.prepare(moved,batch,plan);
    require(renderer.work().decodes==1,"Unrelated OAM change traversed background tiles");
    auto palette=std::make_shared<simulation::SnesPpuState>(*moved);palette->cgram[17]=31;
    frame=renderer.prepare(palette,batch,plan,7);
    require(renderer.work().decodes==1 && renderer.work().colour_updates==2,"Palette-only fade did not reuse source indices");
    require(pixel(frame.textures[0],200,100)==std::array<unsigned,4>{119,0,0,255},"Native integer brightness/fade mismatch");
    require(frame.textures[0].source_layers.data()==ownership && frame.textures[0].source_layers[100*464+200]==2,
        "Palette-only fade re-decoded/reallocated source-layer coverage");
    frame=renderer.prepare(palette,batch,plan,15,10);
    require(pixel(frame.textures[0],200,100)==std::array<unsigned,4>{173,0,0,255},"BG2 native colour subtraction mismatch");
    auto rolled=std::make_shared<simulation::SnesPpuState>(*palette);
    rolled->bg2_horizontal_offsets_enabled=true;rolled->bg2_vertical_offsets_enabled=true;
    rolled->bg2_horizontal_offsets.fill(-37);
    for(unsigned i=0;i<32;++i) {rolled->vram[0x5f40+i*2]=7;rolled->vram[0x5f40+i*2+1]=64;}
    renderer.prepare(rolled,batch,plan);
    require(renderer.work().decodes==2,"HDMA/offset updates did not invalidate source raster");
    const auto work=renderer.work();const auto retained=renderer.prepare(rolled,batch,plan);
    const std::vector<std::uint8_t> saved(retained.textures[0].pixels.begin(),retained.textures[0].pixels.end());
    auto bad=batch;bad.passes[0].priority=3;
    rejected([&]{renderer.prepare(rolled,bad,plan);},"Invalid priority was silently substituted");
    require(std::equal(saved.begin(),saved.end(),retained.textures[0].pixels.begin()) && renderer.work().decodes==work.decodes,"Failed source pass invalidated previous layer");
    auto impossible=plan;impossible.eyes[0].projection_offset=-33;
    rejected([&]{renderer.prepare(rolled,batch,impossible);},"Scenery outside allocated eye coverage accepted");
    require(*ppu==original,"Layer preparation mutated cartridge data");
    PicaRaster overlay;PpuBatch ui;ui.passes.push_back({PpuLayer::objects});ui.passes[0].sprites=render::SpriteSelection::world_only;
    const auto hud=overlay.prepare(ppu,ui,plan);
    require(hud.draws[0].space==PicaSpace::screen && pixel(hud.textures[0],10,18)[3]==0,"Moved lower-screen HUD retained on upper LCD");
    require(pixel(hud.textures[0],100,188)==std::array<unsigned,4>{255,0,0,255},"Top-world reticle lost while moving HUD");
    require(pixel(hud.textures[0],190,93)==std::array<unsigned,4>{255,0,0,255},"Top-world explosion OBJ lost");
    require(pixel(hud.textures[0],0,0)[3]==0 && pixel(hud.textures[0],100,239)[3]==0,"Screen artwork grew opaque vertical guards");
    PpuBatch order;order.passes={{PpuLayer::bg2},{PpuLayer::bg1,1}};
    const auto green=overlay.prepare(ppu,order,plan);
    require(pixel(green.textures[0],100,100)==std::array<unsigned,4>{0,255,0,255},"Painter order/priority did not retain opaque foreground");
    order.passes[1].priority=0;
    const auto black=overlay.prepare(ppu,order,plan);
    require(pixel(black.textures[0],100,100)==std::array<unsigned,4>{0,0,0,255},"Low-priority source pass painted high tile");
    auto mode3=std::make_shared<simulation::SnesPpuState>(*ppu);mode3->background_mode=3;
    tile(*mode3,0x4400,1,2,8);
    const auto map=overlay.prepare(mode3,PpuBatch{{{PpuLayer::bg1,1}}},plan);
    require(pixel(map.textures[0],100,100)==std::array<unsigned,4>{0,255,0,255},"Mode-3 8bpp map artwork lost");
    auto mode1=std::make_shared<simulation::SnesPpuState>(*ppu);mode1->background_mode=1;
    const auto bg3=overlay.prepare(mode1,PpuBatch{{{PpuLayer::bg3}}},plan);
    require(pixel(bg3.textures[0],100,100)==std::array<unsigned,4>{0,0,255,255},"Mode-1 2bpp BG3 artwork lost");
    require(bg3.textures[0].source_layers[100*256+100]==4,"BG3 lost its distinct colour-math source layer");
    auto mixed=std::make_shared<simulation::SnesPpuState>(*ppu);
    for(unsigned y=0;y<32;++y) for(unsigned x=0;x<16;++x) mixed->vram[0xc800+(y*32+x)*2]=0;
    const auto group=overlay.prepare(mixed,PpuBatch{{{PpuLayer::bg2},{PpuLayer::bg1},{PpuLayer::objects}}},plan);
    const auto& texture=group.textures[0];
    require(texture.source_layers[100*256+5]==2 && texture.source_layers[100*256+200]==1
        && texture.source_layers[93*256+191]==16,"Mixed painter group lost winning BG2/BG1/OBJ provenance");
    require(pixel(texture,5,100)==std::array<unsigned,4>{0,0,0,255} && texture.source_layers[5]==0,
        "Opaque black and uncovered vertical guard ownership confused");
    require(validate_pica_layers(texture)==19,"Mixed PPU group did not retain precisely its occupied source classes");
}
void composition() {
    const auto plan=plan_frame(1,true,ScreenUse::world);
    Canvas dashboard;const auto ppu=source();PicaRaster sky,ui;
    PpuBatch sky_batch{{{PpuLayer::bg2}},PicaSpace::scenery,true};
    PpuBatch ui_batch{{{PpuLayer::objects}}};
    const auto backdrop=sky.prepare(ppu,sky_batch,plan),overlay=ui.prepare(ppu,ui_batch,plan);
    const std::array<PicaVertex,3> vertices{{{{-1,-1,512}},{{1,-1,512}},{{0,1,512}}}};
    const std::array<PicaDraw,1> draws{{{0,3}}};
    const PicaFrame models{plan,vertices,draws,{}};
    const std::array groups{backdrop,models,overlay};PicaComposite compositor;
    const auto frame=compositor.prepare(plan,groups,dashboard.view());
    validate_pica_frame(frame,dashboard.view());
    require(frame.draws.size()==3 && frame.draws[0].space==PicaSpace::scenery && frame.draws[1].space==PicaSpace::world
        && frame.draws[2].space==PicaSpace::screen,"Compositor flattened/reordered depth and foreground groups");
    require(frame.draws[0].texture==0 && frame.draws[1].texture==pica_no_texture && frame.draws[2].texture==1
        && frame.draws[1].first==6 && frame.draws[2].first==9,"Composed source texture/geometry offsets wrong");
    require(frame.textures[0].pixels.data()==backdrop.textures[0].pixels.data(),"Compositor copied/repainted source artwork");
    require(frame.textures[0].source_layers.data()==backdrop.textures[0].source_layers.data(),"Compositor copied/repainted cached source ownership");
    const std::vector<PicaVertex> saved(frame.vertices.begin(),frame.vertices.end());
    auto wrong=models;wrong.plan.slider=0;
    rejected([&]{compositor.prepare(plan,std::array{backdrop,wrong,overlay},dashboard.view());},"Mismatched eye plans combined");
    require(std::equal(saved.begin(),saved.end(),frame.vertices.begin()),"Failed composition discarded previous native geometry");
    std::array<PicaFrame,8> oversized;oversized.fill(backdrop);
    rejected([&]{compositor.prepare(plan,oversized,dashboard.view());},"Combined padded texture budget not checked");
    const auto layers=compositor.prepare_layers(plan,groups);
    validate_pica_frame(layers,dashboard.view());
    require(layers.vertices.size()==saved.size() && std::equal(saved.begin(),saved.end(),layers.vertices.begin())
        && layers.textures[0].pixels.data()==backdrop.textures[0].pixels.data(),"Dashboard-independent composition copied or flattened painter groups");
    rejected([&]{compositor.prepare_layers(plan,oversized);},"Artwork-only composition forgot the reserved lower LCD texture");
    rejected([&]{validate_pica_group(models,pica_texture_budget+1);},"Group validator accepted overflowing reserved residency");
}
void colour_effects() {
    const auto plan=plan_frame(1,true,ScreenUse::world);Canvas dashboard;PicaColourEffects effects;
    simulation::CircleEffectState circle;simulation::ColourMathEffectState math;
    require(effects.prepare(circle,math,15,plan).draws.empty(),"Inactive source effects generated a colour pass");
    circle.active=true;circle.red=31;circle.green=15;circle.blue=7;circle.affected_layers=3;
    const auto check=[&](int cx,int cy,unsigned radius,std::optional<PicaClip> clip) {
        circle.centre_x=std::int16_t(cx);circle.centre_y=std::int16_t(cy);circle.radius=std::uint16_t(radius);
        const auto frame=effects.prepare(circle,math,15,plan,clip);validate_pica_frame(frame,dashboard.view());
        require(frame.textures.empty() && frame.draws.size()<=1,"Circle flattened the world or allocated a mask bitmap/per-row draw");
        if(!frame.draws.empty()) require(frame.draws[0].colour_op==PicaColourOp{false,false,3}
            && frame.draws[0].source_layer==0 && frame.draws[0].clip==clip,"Circle lost source CGADSUB/clip contract");
        std::vector<unsigned char> coverage(top_width*screen_height,0);
        for(unsigned i=0;i<frame.vertices.size();i+=6) {
            const auto& a=frame.vertices[i];const auto& b=frame.vertices[i+2];
            const int left=int(a.position[0]),top=int(a.position[1]),right=int(b.position[0]),bottom=int(b.position[1]);
            require(left>=0 && top>=0 && left<right && top<bottom && right<=400 && bottom<=240,"Colour rectangle outside LCD");
            for(int y=top;y<bottom;++y) for(int x=left;x<right;++x) ++coverage[y*400+x];
        }
        const auto bounds=clip.value_or(PicaClip{});
        for(int y=0;y<240;++y) for(int x=0;x<400;++x) {
            const std::int64_t dx=x-cx-72,dy=y-cy-8;
            const bool inside=radius && dx*dx+dy*dy<=std::int64_t(radius)*radius
                && x>=bounds.left && x<bounds.right && y>=bounds.top && y<bounds.bottom;
            require(coverage[y*400+x]==unsigned(inside),"Native disk differs from independent source pixel circle (overlap/edge/clip)");
        }
        const auto builds=effects.builds();const auto* vertices=frame.vertices.data();
        for(float slider:{0.F,.5F,1.F}) {
            const auto other=effects.prepare(circle,math,15,plan_frame(slider,true,ScreenUse::world),clip);
            require(other.vertices.data()==vertices && effects.builds()==builds,"Slider/second eye rebuilt colour coverage");
        }
    };
    for(unsigned radius:{0U,1U,2U,31U,96U,240U,65535U}) check(128,112,radius,{});
    check(-32768,-32768,65535,{});check(32767,32767,1,{});check(-73,-8,30,{});
    check(128,112,80,PicaClip{140,80,260,180});
    circle.centre_x=128;circle.centre_y=112;circle.radius=40;
    math.active=true;math.subtract=true;math.half=true;math.affected_layers=0x2f;math.red=31;math.green=5;math.blue=0;
    for(unsigned brightness=0;brightness<16;++brightness) {
        const auto frame=effects.prepare(circle,math,brightness,plan);validate_pica_frame(frame,dashboard.view());
        require(frame.draws.size()==2 && frame.draws[0].colour_op==PicaColourOp{false,false,3}
            && frame.draws[1].colour_op==PicaColourOp{true,true,0x2f},"Source circle/global colour math order or add/sub/half semantics lost");
        for(unsigned channel=0;channel<3;++channel) {
            const unsigned five=std::array<unsigned,3>{31,15,7}[channel];
            const auto expanded=((five<<3)|(five>>2))*brightness/15;
            const auto fixed=expanded>>3;const auto expected=(fixed<<3)|(fixed>>2);
            require(std::abs(frame.vertices[0].colour[channel]*255-expected)<.001F,"Circle fixed-colour brightness/5-bit conversion mismatch");
        }
        require(frame.vertices[frame.draws[1].first].colour[0]==1,"Global fixed-colour fade incorrectly scaled by source brightness");
        const auto last=frame.draws[1].first;
        require(frame.vertices[last].position==Point3{0,0,0} && frame.vertices[last+2].position==Point3{400,240,0},
            "Full-screen source flash/fade left uncovered LCD margins");
    }
    for(unsigned flags:{0U,64U,128U,192U}) {
        circle.affected_layers=std::uint8_t(flags|17);
        const auto frame=effects.prepare(circle,math,15,plan);
        require(frame.draws[0].colour_op==PicaColourOp{bool(flags&128),bool(flags&64),17},"Circle CGADSUB high bits not decoded independently");
    }
    const auto retained=effects.prepare(circle,math,15,plan);const auto builds=effects.builds();
    const std::vector<PicaVertex> saved(retained.vertices.begin(),retained.vertices.end());
    rejected([&]{effects.prepare(circle,math,16,plan);},"Unsupported brightness accepted");
    rejected([&]{effects.prepare(circle,math,15,plan,PicaClip{0,0,0,240});},"Empty source circle clip accepted");
    require(effects.builds()==builds && std::equal(saved.begin(),saved.end(),retained.vertices.begin()),"Failed colour preparation discarded last complete native pass");
    auto bad_draw=retained.draws.front();bad_draw.colour_op->layers=0;
    PicaFrame bad{plan,std::span(retained.vertices).first(bad_draw.count),std::span(&bad_draw,1),{}};
    rejected([&]{validate_pica_frame(bad,dashboard.view());},"Colour operation without selected source layers accepted");
    bad_draw=retained.draws.front();bad_draw.source_layer=1;
    rejected([&]{validate_pica_frame(bad,dashboard.view());},"Colour operation may not replace source ownership");
    bad_draw=retained.draws.front();bad_draw.alpha_blend=true;
    rejected([&]{validate_pica_frame(bad,dashboard.view());},"Ordinary opacity may not silently override colour-math blend");
    circle.active=false;math.active=false;
    require(effects.prepare(circle,math,15,plan).vertices.empty(),"Retired source colour math left stale damage/death effects");
    circle.active=true;circle.affected_layers=128;circle.radius=40;
    require(effects.prepare(circle,math,15,plan).draws.empty(),"Circle with no affected source layers generated a blend pass");
    // Exhaust each source-mask selector from a real prepared operation. This
    // proves the native contract; physical PICA stencil/colour pixels remain
    // a separate acceptance test, not inferred from this portable oracle.
    for(unsigned selected=1;selected<64;++selected) {
        circle.affected_layers=std::uint8_t(selected|128);math.active=false;
        const auto selected_frame=effects.prepare(circle,math,15,plan);
        const auto op=*selected_frame.draws[0].colour_op;
        for(unsigned bit=0;bit<6;++bit) require(bool(op.layers&(1U<<bit))==bool((selected>>bit)&1),
            "Prepared colour operation included/excluded wrong source layer");
        require(op.subtract && !op.half && selected_frame.draws[0].source_layer==0,
            "Prepared subtract operation unexpectedly halves or overwrites source ownership");
    }
}
bool expected_mask(const simulation::WindowWipeState& wipe,WindowCoverage coverage,unsigned x,unsigned y) {
    if(!wipe.active) return false;
    const bool wide=coverage==WindowCoverage::full_scene;
    const int sx=wide?16+int(x*223/399):int(x)-72;
    if(wipe.horizontal_opening) {
        const double sy=wide?(double(y)+.5)*.8:double(y)+.5-24;
        if(sy<0 || sy>=192) return false;
        if(sy<wipe.opening_top || sy>=wipe.opening_bottom) return true;
        return wide?x<2:(!(sx>=15 && sx<=16))!=(sx>=16 && sx<=240);
    }
    const int sy=wide?int(y*191/239):int(y)-24;
    if(sy<0 || sy>=192) return false;
    const int left=std::uint8_t(wipe.left[sy]),right=std::uint8_t(wipe.right[sy]);
    const bool dynamic=left<=right?(sx>=left && sx<=right):(sx>=left || sx<=right);
    const bool first=!dynamic,second=sx>=16 && sx<=240;
    if((wipe.logic&3)==0) return first || second;
    if((wipe.logic&3)==1) return first && second;
    if((wipe.logic&3)==2) return first!=second;
    return first==second;
}
void window_masks() {
    const auto plan=plan_frame(1,true,ScreenUse::world);Canvas dashboard;PicaWindow window;
    simulation::WindowWipeState wipe;wipe.active=true;
    const auto check=[&](WindowCoverage coverage) {
        const auto before=wipe;
        const auto frame=window.prepare(wipe,plan,coverage);validate_pica_frame(frame,dashboard.view());
        require(frame.textures.empty() && frame.draws.size()<=1,"Source wipe allocated a full world/mask bitmap or one draw per row");
        if(!frame.draws.empty()) require(frame.draws[0].space==PicaSpace::screen && !frame.draws[0].depth_test
            && !frame.draws[0].depth_write && !frame.draws[0].alpha_blend,"Black source wipe lost post-composition screen role");
        std::vector<std::uint8_t> actual(top_width*screen_height,0);
        for(unsigned i=0;i<frame.vertices.size();i+=6) {
            const auto& a=frame.vertices[i];const auto& b=frame.vertices[i+2];
            const unsigned left=unsigned(a.position[0]),top=unsigned(a.position[1]);
            const unsigned right=unsigned(b.position[0]),bottom=unsigned(b.position[1]);
            require(left<right && top<bottom && right<=400 && bottom<=240,"Native wipe emits out-of-bounds/empty rectangle");
            for(unsigned j=i;j<i+6;++j) require(frame.vertices[j].colour==std::array<float,4>{0,0,0,1},"Wipe rectangle not opaque black");
            for(unsigned y=top;y<bottom;++y) for(unsigned x=left;x<right;++x) ++actual[y*400+x];
        }
        for(unsigned y=0;y<240;++y) for(unsigned x=0;x<400;++x)
            require(actual[y*400+x]==unsigned(expected_mask(wipe,coverage,x,y)),
                "Native source-window geometry disagrees with independent per-pixel logic or leaves an edge seam");
        const auto builds=window.builds();const auto* data=frame.vertices.data();
        for(float slider:{0.F,.5F,1.F}) {
            const auto other=window.prepare(wipe,plan_frame(slider,true,ScreenUse::world),coverage);
            require(window.builds()==builds && other.vertices.data()==data,"Slider/second eye rebuilt source window mask");
        }
        require(wipe.left==before.left && wipe.right==before.right && wipe.opening_top==before.opening_top
            && wipe.opening_bottom==before.opening_bottom,"Window preparation mutated native raster state");
    };
    for(unsigned logic=0;logic<4;++logic) for(unsigned phase=0;phase<5;++phase) {
        wipe.logic=logic;
        for(unsigned y=0;y<192;++y) {
            wipe.left[y]=std::uint16_t((y*13+phase*59)&511);
            wipe.right[y]=std::uint16_t((y*7+255-phase*39)&511);
        }
        check(WindowCoverage::authored);check(WindowCoverage::full_scene);
    }
    wipe.logic=1;wipe.left.fill(0);wipe.right.fill(0);check(WindowCoverage::full_scene);
    const auto closed=window.prepare(wipe,plan,WindowCoverage::full_scene);
    require(closed.vertices.size()==6 && closed.vertices[0].position==Point3{0,0,0}
        && closed.vertices[2].position==Point3{400,240,0},"Closed Training wipe must cover every added LCD column/row");
    wipe.horizontal_opening=true;
    for(double edge:{0.,32.125,63.5,96.}) {
        wipe.opening_top=edge;wipe.opening_bottom=192-edge;
        check(WindowCoverage::authored);check(WindowCoverage::full_scene);
    }
    const auto retained=window.prepare(wipe,plan,WindowCoverage::full_scene);
    const std::vector<PicaVertex> saved(retained.vertices.begin(),retained.vertices.end());const auto builds=window.builds();
    auto bad=wipe;bad.opening_top=std::numeric_limits<double>::quiet_NaN();
    rejected([&]{window.prepare(bad,plan,WindowCoverage::full_scene);},"Non-finite shutter accepted");
    require(window.builds()==builds && std::equal(saved.begin(),saved.end(),retained.vertices.begin()),"Failed window transaction lost last complete mask");
    const std::array<PicaVertex,3> geometry{{{{-1,-1,512}},{{1,-1,512}},{{0,1,512}}}};
    const std::array<PicaDraw,1> draw{{{0,3}}};const PicaFrame models{plan,geometry,draw,{}};
    PicaComposite composite;const auto composed=composite.prepare(plan,std::array{models,retained},dashboard.view());
    require(composed.draws.front().space==PicaSpace::world && composed.draws.back().space==PicaSpace::screen
        && std::equal(geometry.begin(),geometry.end(),composed.vertices.begin()),"Window flattened or reordered stereo model geometry");
    wipe.active=false;
    require(window.prepare(wipe,plan,WindowCoverage::authored).vertices.empty(),"Retired source wipe left a stale black shutter");
    require(pica_screen_scissor({10,20,30,40})==std::array<unsigned,4>{200,370,220,390},"LCD-to-rotated-target scissor mapping wrong");
    rejected([&]{pica_screen_scissor({0,0,400,241});},"Off-LCD effect scissor accepted");
    rejected([&]{pica_screen_scissor({0,0,0,240});},"Empty effect scissor accepted");
}
}
int main() try {raster();composition();window_masks();colour_effects();std::cout<<checks<<" 3DS native PPU/cache/composition checks passed; NOT full game/hardware acceptance\n";}
catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}

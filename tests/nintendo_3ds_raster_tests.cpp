#include "starfox/platform/nintendo_3ds/pica_raster.hpp"
#include "starfox/platform/nintendo_3ds/pica_composite.hpp"
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
    const std::vector<PicaVertex> saved(frame.vertices.begin(),frame.vertices.end());
    auto wrong=models;wrong.plan.slider=0;
    rejected([&]{compositor.prepare(plan,std::array{backdrop,wrong,overlay},dashboard.view());},"Mismatched eye plans combined");
    require(std::equal(saved.begin(),saved.end(),frame.vertices.begin()),"Failed composition discarded previous native geometry");
    std::array<PicaFrame,8> oversized;oversized.fill(backdrop);
    rejected([&]{compositor.prepare(plan,oversized,dashboard.view());},"Combined padded texture budget not checked");
}
}
int main() try {raster();composition();std::cout<<checks<<" 3DS native PPU/cache/composition checks passed; NOT full game/hardware acceptance\n";}
catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}

#include "starfox/platform/nintendo_3ds/game_layers.hpp"
#include <iostream>

namespace {
using namespace starfox;
using namespace platform::nintendo_3ds;
unsigned checks{};
void require(bool value,const char* message) {++checks;if(!value) throw std::runtime_error(message);}
GamePresentation source(simulation::GameFlowState flow,unsigned mode) {
    auto scene=std::make_shared<vr::GameSceneSnapshot>();scene->flow=flow;
    auto ppu=std::make_shared<simulation::SnesPpuState>();ppu->background_mode=std::uint8_t(mode);
    ppu->main_screen=23;ppu->bg1_screen_size=ppu->bg2_screen_size=ppu->bg3_screen_size=0;
    // PPU base registers are VRAM WORD addresses; fixture writes below are bytes.
    ppu->bg1_character_base=0;ppu->bg2_character_base=0x1000;ppu->bg3_character_base=0x2000;
    ppu->bg1_screen_base=0x3000;ppu->bg2_screen_base=0x3200;ppu->bg3_screen_base=0x3400;
    for(unsigned number=0;number<128;++number) ppu->oam[number*4+1]=240;
    for(unsigned i=0;i<256;++i) ppu->cgram[i]=std::uint16_t((i%32)|(((i*3)%32)<<5)|(((i*7)%32)<<10));
    for(unsigned row=0;row<8;++row) {
        ppu->vram[(mode==3?64:32)+row*2]=255;
        ppu->vram[0x2000+32+row*2]=255;ppu->vram[0x4000+16+row*2]=255;
    }
    for(unsigned layer=0;layer<3;++layer) for(unsigned col:{7U,8U}) {
        const unsigned map=0x6000+layer*0x400,at=map+(5*32+col)*2;
        const unsigned tile=1|((layer+1)<<10)|(col==8?0x2000:0);
        ppu->vram[at]=std::uint8_t(tile);ppu->vram[at+1]=std::uint8_t(tile>>8);
    }
    auto raster=std::make_shared<GameRasterSnapshot>();raster->ppu=ppu;raster->brightness=15;
    GamePresentation frame;frame.current=frame.previous=scene;frame.raster=raster;
    frame.plan=plan_frame(0,true,ScreenUse::front_end);return frame;
}
std::pair<std::array<std::uint8_t,4>,unsigned> pixel(const PicaFrame& group,unsigned x,unsigned y) {
    if(group.textures.empty()) return {};
    const auto& image=group.textures.front();const auto at=(std::size_t(y+8)*image.width+(image.width-256)/2+x);
    return {{{image.pixels[at*4],image.pixels[at*4+1],image.pixels[at*4+2],image.pixels[at*4+3]}},image.source_layers[at]};
}
bool has(const PpuBatch& batch,PpuLayer layer,int priority) {
    return std::any_of(batch.passes.begin(),batch.passes.end(),[&](const auto& pass){return pass.layer==layer && pass.priority==priority;});
}
void priority_pixels() {
    using enum simulation::GameFlowState;
    for(unsigned mode:{1U,2U,3U}) {
        auto frame=source(planet_select,mode);GameLayers layers;const auto prepared=layers.prepare(frame);
        const auto low=pixel(prepared.before_models,56,40),high=pixel(prepared.before_models,64,40);
        require(low.first[3]==255 && high.first[3]==255,"Source priority cells lost opaque coverage");
        require(low.second==(mode==3?1U:2U) && high.second==(mode==3?1U:2U),"BG1 map buffer/BG2 painter order changed");
        if(mode==3) require(!has(game_layer_plan(frame).before_models,PpuLayer::bg3,0),"Mode 3 injected nonexistent BG3");
    }
    auto frame=source(title,1);GameLayers title_layers;auto result=title_layers.prepare(frame);
    require(pixel(result.before_models,64,40).second==2,"Title backdrop must precede models");
    require(pixel(result.after_models,56,40).second==1 && pixel(result.after_models,64,40).second==4,
        "Title must restore BG1 text then BG3 high over BG2 high/models");
    auto ppu=std::make_shared<simulation::SnesPpuState>(*frame.raster->ppu);ppu->cgram[17]=0;
    auto raster=std::make_shared<GameRasterSnapshot>(*frame.raster);raster->ppu=ppu;frame.raster=raster;
    result=title_layers.prepare(frame);
    require(pixel(result.after_models,56,40).first==std::array<std::uint8_t,4>{0,0,0,255},"Title black text/outline became transparent");
    for(auto flow:{controls_type,controls_choice,continue_choice}) {
        frame=source(flow,1);GameLayers layers;result=layers.prepare(frame);
        require(pixel(result.after_models,56,40).first[3]==0 && pixel(result.after_models,64,40).second==2,
            "Controls/Continue restored low-priority backdrop over models or lost high frame");
    }
    for(unsigned mode:{1U,2U}) {
        frame=source(ex_pregame_menu,mode);GameLayers layers;result=layers.prepare(frame);
        require(pixel(result.before_models,56,40).second==1 && pixel(result.before_models,64,40).second==1,
            "EX menu lost native BG1 low/high text");
        const auto plan=game_layer_plan(frame);
        for(const auto& pass:plan.before_models.passes) if(pass.layer==PpuLayer::bg1)
            require(!pass.extend_horizontal && pass.guard_inset==16,"EX menu extended guard columns as artwork");
    }
}
void policy_contracts() {
    using enum simulation::GameFlowState;
    auto frame=source(title,1);auto scene=std::make_shared<vr::GameSceneSnapshot>(*frame.current);
    scene->meters.extended=true;scene->ex_title_logo_screen=true;frame.current=scene;
    auto plan=game_layer_plan(frame);
    require(!has(plan.after_models,PpuLayer::bg1,-1),"EX animated logo covered native host model with BG1 bitmap");
    for(const auto& pass:plan.before_models.passes) if(pass.layer==PpuLayer::bg2)
        require(pass.extend_horizontal && !pass.wrap_horizontal,"EX title repeated logo atlas in margins");
    scene->ex_title_logo_screen=false;plan=game_layer_plan(frame);
    require(has(plan.after_models,PpuLayer::bg1,-1),"Regular EX title lost authored text");
    frame=source(gameplay,2);frame.sprites=render::SpriteSelection::world_only;
    plan=game_layer_plan(frame);
    require(plan.before_models.passes.size()==1 && plan.before_models.passes.front().layer==PpuLayer::bg2,
        "Mode 2 gameplay did redundant early OBJ priority passes");
    require(plan.after_models.passes.size()==4,"Gameplay world OBJ priorities not restored above models");
    for(const auto& pass:plan.after_models.passes)
        require(pass.sprites==render::SpriteSelection::world_only,"Gameplay reintroduced moved HUD by erasing a final image");
    scene=std::make_shared<vr::GameSceneSnapshot>(*frame.current);scene->native_ex_bitmap=true;scene->dialogue.active=true;
    frame.current=scene;plan=game_layer_plan(frame);
    require(!has(plan.after_models,PpuLayer::bg1,-1),"Moved EX dialogue duplicated in the upper bitmap");
    scene->flow=intro;frame.sprites=render::SpriteSelection::all;plan=game_layer_plan(frame);
    require(has(plan.after_models,PpuLayer::bg1,-1),"Intro lost dialogue before lower-HUD routing begins");
    scene->dialogue.active=false;scene->paused=true;plan=game_layer_plan(frame);
    for(const auto& pass:plan.after_models.passes) if(pass.layer==PpuLayer::bg1)
        require(pass.guard_inset==16 && pass.transparent_black && pass.mosaic_inset,"EX pause bitmap staging rules changed");
    frame=source(credits,1);auto raster=std::make_shared<GameRasterSnapshot>(*frame.raster);raster->boss_roll=true;frame.raster=raster;
    plan=game_layer_plan(frame);
    require(has(plan.after_models,PpuLayer::objects,2) && has(plan.after_models,PpuLayer::bg2,1),"Boss roll lost Continue-style foreground frame");
}
void margins_and_cache() {
    auto frame=source(simulation::GameFlowState::controls_type,1);
    auto ppu=std::make_shared<simulation::SnesPpuState>(*frame.raster->ppu);
    // Native right field is BG2 palette 2, deliberately unlike CGRAM zero and
    // the left BG3 demonstration surround. Only the right field may extend.
    for(unsigned row=0;row<28;++row) {
        const auto right=0x6400+(row*32+31)*2,left=0x6800+(row*32)*2;
        ppu->vram[right]=1;ppu->vram[right+1]=8;ppu->vram[left]=1;ppu->vram[left+1]=12;
    }
    auto raster=std::make_shared<GameRasterSnapshot>(*frame.raster);raster->ppu=ppu;frame.raster=raster;
    GameLayers layers;auto prepared=layers.prepare(frame);const auto field=pixel(prepared.before_models,255,40).first;
    require(prepared.clear==Rgb{field[0],field[1],field[2]},"Controls clear took miscolored native left edge/backdrop");
    const auto cached=layers.work();
    for(float slider:{0.F,.5F,1.F}) {
        frame.plan=plan_frame(slider,true,ScreenUse::world);prepared=layers.prepare(frame);
        const auto work=layers.work();
        require(work[0].decodes==cached[0].decodes && work[1].decodes==cached[1].decodes
            && work[0].colour_updates==cached[0].colour_updates && work[1].colour_updates==cached[1].colour_updates,
            "Slider rebuilt source priority artwork");
        Canvas lower;validate_pica_frame(prepared.before_models,lower.view());validate_pica_frame(prepared.after_models,lower.view());
    }
    raster=std::make_shared<GameRasterSnapshot>(*frame.raster);raster->brightness=7;frame.raster=raster;
    prepared=layers.prepare(frame);const auto faded=layers.work();
    require(faded[0].decodes==cached[0].decodes && faded[0].colour_updates==cached[0].colour_updates+1,
        "Brightness fade reran tile priorities instead of recoloring coverage");
    ppu=std::make_shared<simulation::SnesPpuState>(*ppu);ppu->main_screen=0;
    raster=std::make_shared<GameRasterSnapshot>(*frame.raster);raster->ppu=ppu;frame.raster=raster;
    prepared=layers.prepare(frame);
    require(prepared.before_models.draws.empty() && prepared.after_models.draws.empty(),"Disabled source layers rendered stale cache");
    bool rejected=false;ppu->background_mode=7;
    try {static_cast<void>(layers.prepare(frame));} catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,"Unsupported source mode was silently substituted");
}
}
int main() try {
    priority_pixels();policy_contracts();margins_and_cache();
    std::cout<<checks<<" 3DS actual source painter-policy checks passed; not full terrain/menu/hardware acceptance\n";
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}

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
    std::pair<std::array<std::uint8_t,4>,unsigned> result{};
    for(const auto& draw:group.draws) {
        if(draw.texture==pica_no_texture) continue;
        const auto& image=group.textures[draw.texture];
        if(draw.space==PicaSpace::world) continue;
        const auto& origin=group.vertices[draw.first].position;
        const int ix=int(x)+72-int(origin[0]),iy=int(y)+8-int(origin[1]);
        if(ix<0 || iy<0 || ix>=int(image.width) || iy>=int(image.height)) continue;
        const auto at=std::size_t(iy)*image.pitch+unsigned(ix)*4;
        if(image.pixels[at+3]) result={
            {image.pixels[at],image.pixels[at+1],image.pixels[at+2],image.pixels[at+3]},
            image.source_layers.empty()?draw.source_layer:image.source_layers[std::size_t(iy)*image.layer_pitch+unsigned(ix)]};
    }
    return result;
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
void landscape_depth() {
    auto frame=source(simulation::GameFlowState::gameplay,2);
    auto scene=std::make_shared<vr::GameSceneSnapshot>(*frame.current);
    scene->background_landscape=true;scene->landscape_grid_height=-145;scene->landscape_atlas_origin=232;
    frame.current=frame.previous=scene;
    auto ppu=std::make_shared<simulation::SnesPpuState>(*frame.raster->ppu);
    ppu->bg2_scroll_y=232;ppu->bg2_screen_size=3;ppu->bg2_vertical_offsets_enabled=true;
    for(unsigned i=0;i<4096;++i) {ppu->vram[0x6400+i*2]=1;ppu->vram[0x6401+i*2]=8;}
    // Quantized source HDMA steps, including a signed 8192-word crossing.
    for(int roll:{-16,-3,0,3,16}) {
        const int base=roll<0?8190:roll>0?8060:211;
        for(unsigned i=0;i<32;++i) {
            const unsigned word=0x4000|((base+roll*int(i+1))&8191),at=(0x2fa0+i)*2;
            ppu->vram[at]=std::uint8_t(word);ppu->vram[at+1]=std::uint8_t(word>>8);
        }
        auto raster=std::make_shared<GameRasterSnapshot>(*frame.raster);raster->ppu=std::make_shared<simulation::SnesPpuState>(*ppu);
        frame.raster=raster;
        const auto plane=source_landscape_plane(frame);
        require(std::abs(plane.slope+roll/8.)<1.e-9 && plane.height==145,"Rolled ground inferred from palette, one edge step or arbitrary depth");
        GameLayers layers;frame.plan=plan_frame(1,true,ScreenUse::world);
        auto prepared=layers.prepare(frame);const auto work=layers.work();
        Canvas lower;validate_pica_frame(prepared.before_models,lower.view());
        const auto& group=prepared.before_models;
        require(group.draws.size()==2 && group.draws[0].space==PicaSpace::scenery
            && group.draws[1].space==PicaSpace::world && group.draws[1].depth_test && group.draws[1].depth_write
            && group.draws[1].projected_uv && group.draws[0].source_layer==2 && group.draws[1].source_layer==2,
            "Native landscape stayed a screen-depth HUD image or lost source ownership/depth");
        require(group.textures.size()==1 && group.textures[0].source_layers.empty(),"Landscape duplicated the entire texture or retained an unnecessary mixed-layer mask");
        const std::vector<PicaVertex> saved(group.vertices.begin(),group.vertices.end());
        for(unsigned i=6;i<saved.size();++i) {
            const auto& v=saved[i];const double z=v.position[2];
            const double x=200+256*v.position[0]/z,y=120-256*v.position[1]/z;
            require(std::abs(x+32-v.uv[0]*464)<.001 && std::abs(y-v.uv[1]*240)<.001,
                "Finite terrain lost the mono cartridge pixel registration");
            require(std::abs(z*(y-plane.centre-plane.slope*(x-200))-145*256)<.1,
                "Terrain vertices are not on the same source camera plane");
        }
        // Independent perspective interpolation oracle at interior points.
        // A plain UV sampler incorrectly stretches the native color bands.
        for(unsigned i=6;i+2<saved.size();i+=3) for(const auto weights:std::array<std::array<double,3>,3>{{{.2,.3,.5},{.7,.2,.1},{.1,.6,.3}}}) {
            double reciprocal=0,u=0,v=0,q=0;std::array<double,3> camera{};
            for(unsigned k=0;k<3;++k) {
                const auto& p=saved[i+k];const double inv=weights[k]/p.position[2];reciprocal+=inv;
                u+=inv*p.uv[0]*p.position[2];v+=inv*p.uv[1]*p.position[2];q+=inv*p.position[2];
                for(unsigned axis=0;axis<3;++axis) camera[axis]+=inv*p.position[axis];
            }
            for(auto& axis:camera) axis/=reciprocal;
            require(std::abs(u/q-(200+256*camera[0]/camera[2]+32)/464)<1.e-6
                && std::abs(v/q-(120-256*camera[1]/camera[2])/240)<1.e-6,
                "Homogeneous source UV/Q does not cancel ground perspective stretching");
            for(unsigned eye=0;eye<2;++eye) {
                const auto pixel=project(frame.plan,eye,float(camera[0]),float(camera[1]),float(camera[2]));
                require(pixel.has_value(),"Valid finite source ground disappeared from an eye projection");
            }
        }
        const auto& nearest=*std::min_element(saved.begin()+6,saved.end(),[](const auto& a,const auto& b){return a.position[2]<b.position[2];});
        const auto left=project(frame.plan,0,nearest.position[0],nearest.position[1],nearest.position[2]);
        const auto right=project(frame.plan,1,nearest.position[0],nearest.position[1],nearest.position[2]);
        require(left && right && std::abs((*left)[0]-(*right)[0]-2*background_offset(frame.plan,0))>.01,
            "Ground used only infinite scenery displacement instead of finite stereo disparity");
        for(float slider:{0.F,.5F,1.F}) {
            frame.plan=plan_frame(slider,true,ScreenUse::world);prepared=layers.prepare(frame);
            require(saved.size()==prepared.before_models.vertices.size() && std::equal(saved.begin(),saved.end(),prepared.before_models.vertices.begin()),
                "Slider changed shared terrain vertices instead of eye projection");
            require(layers.work()[0].decodes==work[0].decodes && layers.work()[0].colour_updates==work[0].colour_updates,
                "Slider reran source landscape tile/colour preparation");
            validate_pica_frame(prepared.before_models,lower.view());
        }
    }
    scene->flow=simulation::GameFlowState::ex_pregame_menu;
    require(!native_landscape_scene(frame),"EX menu terrain replaced its authored planar background");
    scene->flow=simulation::GameFlowState::gameplay;ppu->tunnel_scene=true;
    auto raster=std::make_shared<GameRasterSnapshot>(*frame.raster);raster->ppu=ppu;frame.raster=raster;
    require(!native_landscape_scene(frame),"Tunnel artwork was misclassified as outdoor ground");
}
void panorama_depth() {
    using enum simulation::GameFlowState;
    Canvas lower;
    for(unsigned mode:{1U,2U}) for(auto flow:{intro,gameplay,training,planet_travel,stage_results,game_over,finished,credits}) {
        auto frame=source(flow,mode);
        auto ppu=std::make_shared<simulation::SnesPpuState>(*frame.raster->ppu);
        for(unsigned object=0;object<3;++object) {
            ppu->oam[object*4]=std::uint8_t(56+object*4);ppu->oam[object*4+1]=40;
            ppu->oam[object*4+2]=1;ppu->oam[object*4+3]=std::uint8_t(object<<4);
        }
        auto raster=std::make_shared<GameRasterSnapshot>(*frame.raster);raster->ppu=ppu;frame.raster=raster;
        const auto unchanged=*ppu;const auto plan=game_layer_plan(frame);
        require(native_panorama_scene(frame) && !plan.before_model_groups.empty(),"Distant world artwork was left at screen depth");
        std::vector<PpuPass> flattened;
        for(const auto& batch:plan.before_model_groups) {
            for(const auto& pass:batch.passes) {
                require((batch.space==PicaSpace::scenery)==(pass.layer==PpuLayer::bg2),"Screen-space sprite/BG3 moved into the distant sky");
                flattened.push_back(pass);
            }
        }
        require(flattened==plan.before_models.passes,"Coordinate-space split reordered authored low/high OBJ/BG priorities");
        GameLayers layers;PicaRaster mono_oracle;
        auto prepared=layers.prepare(frame);
        const auto expected=mono_oracle.prepare(ppu,plan.before_models,frame.plan,15);
        validate_pica_frame(prepared.before_models,lower.view());
        for(unsigned y=0;y<224;++y) for(unsigned x=0;x<256;++x)
            require(pixel(prepared.before_models,x,y)==pixel(expected,x,y),"Split panorama changed native mono colour/opaque-black/priority pixels");
        const auto cached=layers.work();
        std::vector<std::vector<std::uint8_t>> pixels;
        for(const auto& image:prepared.before_models.textures) pixels.emplace_back(image.pixels.begin(),image.pixels.end());
        unsigned resident=512U*256U*4U;
        for(const auto& image:prepared.before_models.textures) resident+=pica_resident_texture_bytes(image);
        require(resident<=3U*1024U*1024U,"Contiguous panorama consumed more than its bounded LCD/artwork residency");
        for(float slider:{0.F,.5F,1.F}) {
            frame.plan=plan_frame(slider,true,ScreenUse::world);prepared=layers.prepare(frame);
            validate_pica_frame(prepared.before_models,lower.view());
            require(layers.work()[0].decodes==cached[0].decodes && layers.work()[0].colour_updates==cached[0].colour_updates,
                "3D slider reran source panorama raster/colour traversal");
            require(prepared.before_models.textures.size()==pixels.size(),"Slider rebuilt a different artwork sequence");
            for(unsigned i=0;i<pixels.size();++i)
                require(std::equal(pixels[i].begin(),pixels[i].end(),prepared.before_models.textures[i].pixels.begin()),"Slider changed source artwork bytes");
            for(const auto& draw:prepared.before_models.draws) {
                if(draw.space==PicaSpace::scenery) require(draw.source_layer==2
                    && prepared.before_models.textures[draw.texture].source_layers.empty(),"Isolated BG2 retained mixed ownership or redundant resident A8");
                std::array<float,2> xs{},ys{};
                for(unsigned eye=0;eye<frame.plan.eye_count;++eye) {
                    const auto matrix=pica_draw_matrix(frame.plan,eye,draw);
                    const auto& point=prepared.before_models.vertices[draw.first].position;
                    std::array<float,4> clip{};
                    for(unsigned row=0;row<4;++row) {
                        clip[row]=matrix[row][3];
                        for(unsigned axis=0;axis<3;++axis) clip[row]+=matrix[row][axis]*point[axis];
                    }
                    xs[eye]=(1-clip[1]/clip[3])*200;ys[eye]=(1-clip[0]/clip[3])*120;
                }
                if(frame.plan.eye_count==2) {
                    const float disparity=draw.space==PicaSpace::scenery?
                        background_offset(frame.plan,0)-background_offset(frame.plan,1):0;
                    require(std::abs(xs[0]-xs[1]-disparity)<.0001 && std::abs(ys[0]-ys[1])<.0001,
                        "Native eye matrices put distant artwork at screen depth or displaced screen sprites/vertical alignment");
                }
            }
        }
        raster=std::make_shared<GameRasterSnapshot>(*frame.raster);raster->brightness=0;frame.raster=raster;
        prepared=layers.prepare(frame);
        require(layers.work()[0].decodes==cached[0].decodes,"Palette fade redecoded panorama priorities");
        for(const auto& image:prepared.before_models.textures) for(unsigned i=0;i<image.pixels.size();i+=4)
            require(image.pixels[i]==0 && image.pixels[i+1]==0 && image.pixels[i+2]==0,"Fade left stale illuminated panorama pixels");
        require(ppu->vram==unchanged.vram && ppu->oam==unchanged.oam && ppu->cgram==unchanged.cgram,"Panorama preparation mutated source PPU");
        const auto monotonic=layers.work()[0];
        frame=source(controls_type,1);prepared=layers.prepare(frame);
        require(layers.work()[0].decodes>=monotonic.decodes && game_layer_plan(frame).before_model_groups.empty(),
            "Leaving panorama retained split UI policy or reset cumulative work evidence");
    }
    for(unsigned mode:{1U,2U,3U}) for(auto flow:{title,ex_pregame_menu,planet_select,controls_type,controls_choice,continue_choice}) {
        const auto frame=source(flow,mode);
        require(!native_panorama_scene(frame) && game_layer_plan(frame).before_model_groups.empty(),"Menu/map artwork was incorrectly assigned world infinity");
    }
    auto frame=source(gameplay,1);auto scene=std::make_shared<vr::GameSceneSnapshot>(*frame.current);
    frame.current=scene;scene->background_water_surround=true;
    require(!native_panorama_scene(frame),"Water cross-section was mistaken for a distant panorama");
    scene->background_water_surround=false;scene->background_landscape=true;
    require(!native_panorama_scene(frame),"Finite landscape was flattened into distant panorama");
    scene->background_landscape=false;
    auto raster=std::make_shared<GameRasterSnapshot>(*frame.raster);frame.raster=raster;raster->boss_roll=true;
    require(!native_panorama_scene(frame),"Boss-roll frame was treated as world scenery");
    raster->boss_roll=false;auto ppu=std::make_shared<simulation::SnesPpuState>(*raster->ppu);raster->ppu=ppu;ppu->tunnel_scene=true;
    require(!native_panorama_scene(frame),"Corridor artwork was projected at infinity");
}
void receiver_eye_coverage() {
    auto frame=source(simulation::GameFlowState::gameplay,2);
    auto scene=std::make_shared<vr::GameSceneSnapshot>(*frame.current);
    scene->background_landscape=true;scene->landscape_grid_height=-145;scene->landscape_atlas_origin=232;
    frame.current=frame.previous=scene;
    auto ppu=std::make_shared<simulation::SnesPpuState>(*frame.raster->ppu);
    ppu->bg2_scroll_y=232;ppu->bg2_screen_size=3;ppu->bg2_vertical_offsets_enabled=true;
    for(unsigned i=0;i<4096;++i) {ppu->vram[0x6400+i*2]=1;ppu->vram[0x6401+i*2]=8;}
    Canvas lower;
    for(int roll:{-3,0,3}) for(float convergence:{16.F,32.F,1024.F}) {
        for(unsigned i=0;i<32;++i) {
            const unsigned word=0x4000|((232-roll*16+roll*int(i+1))&8191),at=(0x2fa0+i)*2;
            ppu->vram[at]=std::uint8_t(word);ppu->vram[at+1]=std::uint8_t(word>>8);
        }
        auto raster=std::make_shared<GameRasterSnapshot>(*frame.raster);raster->ppu=std::make_shared<simulation::SnesPpuState>(*ppu);frame.raster=raster;
        StereoSettings settings;settings.strength=2;settings.separation=64;settings.convergence=convergence;
        frame.plan=plan_frame(1,true,ScreenUse::world,settings);GameLayers layers;
        const auto group=layers.prepare(frame).before_models;validate_pica_frame(group,lower.view());
        const auto plane=source_landscape_plane(frame);bool finite_started=false;
        for(const auto& draw:group.draws) {
            if(draw.space==PicaSpace::world) finite_started=true;
            else require(!finite_started,"Infinity strip was painted after finite ground");
        }
        for(unsigned eye=0;eye<2;++eye) for(unsigned y=0;y<240;++y) for(unsigned x=0;x<400;x+=3) {
            const double px=x+.5,py=y+.5,motion=256.*frame.plan.eyes[eye].x,offset=frame.plan.eyes[eye].projection_offset;
            const double sx=(px-offset+motion*(py-plane.centre+200*plane.slope)/(145*256))
                /(1+motion*plane.slope/(145*256));
            const double reciprocal=(py-plane.centre-plane.slope*(sx-200))/(145*256);
            if(reciprocal<1./65536 || reciprocal>1) continue;
            bool covered=false;
            for(const auto& draw:group.draws) if(draw.space==PicaSpace::world) {
                const auto& image=group.textures[draw.texture];const double left=(400.-image.pitch/4)/2+draw.texture*1024;
                for(unsigned i=draw.first;i<draw.first+draw.count;i+=3) {
                    std::array<std::array<double,2>,3> triangle;
                    for(unsigned k=0;k<3;++k) {
                        const auto& v=group.vertices[i+k];const auto& p=v.position;
                        triangle[k]={200+256.*(p[0]-frame.plan.eyes[eye].x)/p[2]+offset,120-256.*p[1]/p[2]};
                        require(std::abs(200+256.*p[0]/p[2]-left-v.uv[0]*image.width)<.02,
                            "Finite strip lost homogeneous source pixel registration");
                    }
                    bool positive=true,negative=true;
                    for(unsigned k=0;k<3;++k) {
                        const auto a=triangle[k],b=triangle[(k+1)%3];
                        const double cross=(b[0]-a[0])*(py-a[1])-(b[1]-a[1])*(px-a[0]);
                        positive&=cross>=-.02;negative&=cross<=.02;
                    }
                    covered|=positive || negative;
                }
            }
            require(covered,"Visible finite receiver pixel fell outside both-eye source coverage");
        }
    }
}
}
int main() try {
    priority_pixels();policy_contracts();margins_and_cache();landscape_depth();panorama_depth();receiver_eye_coverage();
    std::cout<<checks<<" 3DS actual source painter-policy checks passed; not full terrain/menu/hardware acceptance\n";
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}

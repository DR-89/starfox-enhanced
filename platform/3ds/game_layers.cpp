#include "starfox/platform/nintendo_3ds/game_layers.hpp"
#include <map>

namespace starfox::platform::nintendo_3ds {
namespace {
void validate(const GamePresentation& frame) {
    if(!frame.current || !frame.raster || !frame.raster->ppu)
        throw std::invalid_argument("Missing 3DS cartridge painter snapshot");
    const auto mode=frame.raster->ppu->background_mode;
    if(mode<1 || mode>3) throw std::invalid_argument("Unsupported 3DS cartridge painter mode");
}
Rgb backdrop(std::uint16_t word,unsigned brightness) {
    if(brightness>15) throw std::invalid_argument("Invalid 3DS cartridge brightness");
    const auto channel=[&](unsigned shift) {
        const auto five=(word>>shift)&31;
        return std::uint8_t(((five<<3)|(five>>2))*brightness/15);
    };
    return {channel(0),channel(5),channel(10)};
}
Rgb right_margin(const GameLayerFrames& layers) {
    // Controls' native left edge contains the demonstration frame, not the
    // background field. Continue both margins using the dominant right edge.
    std::map<std::array<std::uint8_t,3>,unsigned> counts;
    for(unsigned y=8;y<232;++y) {
        auto colour=layers.clear;
        for(const auto* group:{&layers.before_models,&layers.after_models}) {
            if(group->textures.empty()) continue;
            const auto& image=group->textures.front();
            const auto x=(image.width-256)/2+255;
            const auto at=std::size_t(y)*image.pitch+x*4;
            if(image.pixels[at+3]) colour={image.pixels[at],image.pixels[at+1],image.pixels[at+2]};
        }
        ++counts[{colour.r,colour.g,colour.b}];
    }
    const auto selected=std::max_element(counts.begin(),counts.end(),[](const auto& a,const auto& b){return a.second<b.second;})->first;
    return {selected[0],selected[1],selected[2]};
}
}
bool native_panorama_scene(const GamePresentation& frame) noexcept {
    if(!frame.current || !frame.raster || !frame.raster->ppu || frame.raster->boss_roll) return false;
    const auto& scene=*frame.current;const auto& ppu=*frame.raster->ppu;
    if(ppu.background_mode<1 || ppu.background_mode>2 || ppu.tunnel_scene
        || scene.background_water_surround || scene.background_landscape) return false;
    using enum simulation::GameFlowState;
    return scene.flow==gameplay || scene.flow==training || scene.flow==intro
        || scene.flow==planet_travel || scene.flow==stage_results || scene.flow==game_over
        || scene.flow==finished || scene.flow==credits;
}
GameLayerPlan game_layer_plan(const GamePresentation& frame) {
    validate(frame);
    const auto& scene=*frame.current;const auto& ppu=*frame.raster->ppu;
    using enum simulation::GameFlowState;
    const bool world_hud=scene.flow==gameplay || scene.flow==training;
    const bool title_screen=scene.flow==title;
    const bool ex_menu=scene.flow==ex_pregame_menu;
    const bool controls=scene.flow==controls_type || scene.flow==controls_choice;
    const bool extend=world_hud || scene.flow==intro || scene.flow==planet_travel
        || scene.flow==stage_results || scene.flow==game_over || scene.flow==finished
        || (scene.flow==credits && !frame.raster->boss_roll);
    const bool ex_title=title_screen && scene.meters.extended;
    GameLayerPlan result;
    result.before_models.expand_horizontal=result.after_models.expand_horizontal=true;
    result.solid_frontend_margins=controls || scene.flow==continue_choice;
    const auto pass=[&](PpuLayer layer,int priority,bool horizontal) {
        PpuPass value;value.layer=layer;value.priority=priority;value.extend_horizontal=horizontal;
        if(layer==PpuLayer::objects) value.sprites=frame.sprites;
        if(layer==PpuLayer::bg2) {
            value.scroll=scene.background_scroll_override;value.single_occurrence_top_rows=scene.background_unique_top_rows;
            if(native_landscape_scene(frame)
                && (scene.background_landscape_unique_half || scene.background_landscape_unique_right_half)) {
                if(scene.background_landscape_unique_half && scene.background_landscape_unique_right_half)
                    throw std::invalid_argument("Ambiguous 3DS unique landscape half");
                value.single_occurrence_sky_half=PpuUniqueSkyHalf{scene.background_landscape_unique_right_half,
                    unsigned(scene.landscape_atlas_origin)+112};
            }
        }
        return value;
    };
    const auto bg2=[&](int priority,bool horizontal,bool wrap=true) {
        auto value=pass(PpuLayer::bg2,priority,horizontal);value.wrap_horizontal=wrap;return value;
    };
    const auto native_menu=[&](int priority) {
        auto value=pass(PpuLayer::bg1,priority,false);value.guard_inset=16;return value;
    };
    auto& back=result.before_models.passes;auto& front=result.after_models.passes;
    if(ppu.background_mode==1) {
        back.push_back(pass(PpuLayer::bg3,0,extend || ex_title));
        back.push_back(pass(PpuLayer::objects,0,extend));
        if(!ppu.bg3_high_priority) back.push_back(pass(PpuLayer::bg3,1,extend));
        back.push_back(pass(PpuLayer::objects,1,extend));
        back.push_back(bg2(0,extend || ex_title,!ex_title));
        if(ex_menu) back.push_back(native_menu(0));
        back.push_back(pass(PpuLayer::objects,2,extend));
        back.push_back(bg2(1,extend || ex_title,!ex_title));
        if(ex_menu) back.push_back(native_menu(1));
    } else if(ppu.background_mode==2) {
        if(world_hud) back.push_back(bg2(-1,true));
        else {
            back.push_back(bg2(0,extend));back.push_back(pass(PpuLayer::objects,0,extend));
            if(ex_menu) back.push_back(native_menu(0));
            back.push_back(pass(PpuLayer::objects,1,extend));back.push_back(bg2(1,extend));
            back.push_back(pass(PpuLayer::objects,2,extend));
            if(ex_menu) back.push_back(native_menu(1));
        }
    } else {
        // Mode 3's BG1 is the planet/map buffer, NOT replaceable Super FX
        // model pixels. Retain the full eight-bit source priority sequence.
        back={bg2(0,extend),pass(PpuLayer::objects,0,extend),pass(PpuLayer::bg1,0,extend),
            pass(PpuLayer::objects,1,extend),bg2(1,extend),pass(PpuLayer::objects,2,extend),
            pass(PpuLayer::bg1,1,extend),pass(PpuLayer::objects,3,extend)};
    }
    if(ppu.background_mode==1 && (controls || scene.flow==continue_choice || frame.raster->boss_roll)) {
        if(!controls) front.push_back(pass(PpuLayer::objects,2,extend));
        front.push_back(bg2(1,controls?false:extend));
    }
    if(scene.native_ex_bitmap && !ex_menu
        && !(frame.sprites==render::SpriteSelection::world_only && vr::replace_native_dialogue(scene))) {
        auto bitmap=pass(PpuLayer::bg1,-1,false);
        bitmap.guard_inset=16;bitmap.transparent_black=true;bitmap.mosaic_inset=true;
        front.push_back(bitmap);
    }
    if(frame.raster->stage_hud || world_hud) {
        // World OBJ/explosions stay above models even when the configurable
        // HUD is selected for the lower screen. Selection happens in the source
        // decoder, never by erasing rectangles in a finished world image.
        for(int priority=0;priority<4;++priority) front.push_back(pass(PpuLayer::objects,priority,extend));
    }
    if(title_screen && ppu.background_mode==1) {
        front.push_back(bg2(1,ex_title,!ex_title));
        if(!scene.ex_title_logo_screen) {
            auto text=pass(PpuLayer::bg1,-1,false);text.guard_inset=ex_title?16:0;front.push_back(text);
        }
        front.push_back(pass(PpuLayer::bg3,1,false));
    }
    if(!world_hud) front.push_back(pass(PpuLayer::objects,3,extend));
    if(ppu.background_mode==1 && ppu.bg3_high_priority) front.push_back(pass(PpuLayer::bg3,1,extend));
    if(native_landscape_scene(frame)) result.before_models.space=PicaSpace::scenery;
    else if(native_panorama_scene(frame) || native_water_scene(frame) || native_corridor_scene(frame)) {
        const bool water=native_water_scene(frame),corridor=native_corridor_scene(frame);
        const auto split=[&](const auto& passes,auto& groups) {
            for(const auto& source_pass:passes) {
                const bool receiver=water && source_pass.layer==PpuLayer::bg2;
                const auto space=(source_pass.layer==PpuLayer::bg2 || (water && source_pass.layer==PpuLayer::bg3))
                    ?PicaSpace::scenery:PicaSpace::screen;
                const bool tunnel=corridor && source_pass.layer==PpuLayer::bg2;
                if(groups.empty() || groups.back().space!=space || groups.back().water_receiver!=receiver
                    || groups.back().corridor_receiver!=tunnel) {
                    PpuBatch group;group.space=space;group.expand_horizontal=true;group.water_receiver=receiver;
                    group.corridor_receiver=tunnel;group.compact_strips=(water || corridor) && space==PicaSpace::scenery;
                    groups.push_back(std::move(group));
                }
                groups.back().passes.push_back(source_pass);
            }
        };
        split(back,result.before_model_groups);
        // A Mode-1 high-priority BG3 sky can be after models. Its infinity
        // projection must not move the adjacent OBJ/bitmap passes or their order.
        if(water || corridor) split(front,result.after_model_groups);
    }
    return result;
}
std::array<PpuRasterWork,2> GameLayers::work() const noexcept {
    auto before=retired_before_work_;
    const auto add=[&](PpuRasterWork part){before.decodes+=part.decodes;before.colour_updates+=part.colour_updates;};
    add(before_.work());for(const auto& group:panorama_groups_) add(group->raster.work());
    auto after=retired_after_work_;const auto add_after=[&](PpuRasterWork part) {
        after.decodes+=part.decodes;after.colour_updates+=part.colour_updates;
    };
    add_after(after_.work());for(const auto& group:foreground_groups_) add_after(group->raster.work());
    return {before,after};
}
PicaFrame GameLayers::prepare_groups(const GamePresentation& frame,std::span<const PpuBatch> batches,
    Owners& owners,std::vector<PicaFrame>& working,PicaComposite& composite,PpuRasterWork& retired) {
    while(owners.size()>batches.size()) {
        const auto work=owners.back()->raster.work();retired.decodes+=work.decodes;retired.colour_updates+=work.colour_updates;
        owners.pop_back();
    }
    while(owners.size()<batches.size()) owners.push_back(std::make_unique<PainterOwner>());
    working.clear();working.reserve(owners.size());
    for(unsigned i=0;i<owners.size();++i) {
        auto& owner=*owners[i];const auto& batch=batches[i];
        const auto guard=batch.water_receiver?source_water_guard(frame):batch.corridor_receiver?source_corridor_guard(frame):pica_raster_base_guard;
        auto prepared=owner.raster.prepare(frame.raster->ppu,batch,frame.plan,frame.raster->brightness,
            frame.current->background_colour_subtract,guard,true);
        if(batch.water_receiver) prepared=owner.receiver.prepare_water(frame,prepared,owner.raster.coverage_guard());
        else if(batch.corridor_receiver) prepared=owner.receiver.prepare_corridor(frame,prepared,owner.raster.coverage_guard());
        else if(batch.space==PicaSpace::scenery) {
            auto& images=owner.isolated_images;images.assign(prepared.textures.begin(),prepared.textures.end());
            for(auto& image:images) {image.source_layers={};image.layer_pitch=0;}
            prepared.textures=images;
        }
        working.push_back(prepared);
    }
    return composite.prepare_layers(frame.plan,working);
}
GameLayerFrames GameLayers::prepare(const GamePresentation& frame) {
    const auto policy=game_layer_plan(frame);
    const auto brightness=frame.raster->brightness;
    const auto retire=[&](PpuRasterWork part) {
        retired_before_work_.decodes+=part.decodes;retired_before_work_.colour_updates+=part.colour_updates;
    };
    PicaFrame before;
    if(policy.before_model_groups.empty()) {
        for(const auto& group:panorama_groups_) retire(group->raster.work());
        panorama_groups_.clear();
        unsigned guard=pica_raster_base_guard;
        if(native_landscape_scene(frame)) {
            const auto plane=source_landscape_plane(frame);
            const double distance=plane.height*frame.plan.focal_y;
            guard=pica_receiver_guard(frame.plan,{-plane.slope/distance,1/distance,
                (200*plane.slope-plane.centre)/distance});
        }
        before=before_.prepare(frame.raster->ppu,policy.before_models,frame.plan,brightness,
            frame.current->background_colour_subtract,guard);
    } else {
        retire(before_.work());before_=PicaRaster{};
        before=prepare_groups(frame,policy.before_model_groups,panorama_groups_,working_groups_,panorama_,retired_before_work_);
    }
    PicaFrame after;
    const auto retire_after=[&](PpuRasterWork part) {
        retired_after_work_.decodes+=part.decodes;retired_after_work_.colour_updates+=part.colour_updates;
    };
    if(policy.after_model_groups.empty()) {
        for(const auto& group:foreground_groups_) retire_after(group->raster.work());
        foreground_groups_.clear();
        after=after_.prepare(frame.raster->ppu,policy.after_models,frame.plan,brightness,frame.current->background_colour_subtract);
    } else {
        retire_after(after_.work());after_=PicaRaster{};
        after=prepare_groups(frame,policy.after_model_groups,foreground_groups_,working_foreground_,foreground_,retired_after_work_);
    }
    GameLayerFrames result{before,after,backdrop(frame.raster->ppu->cgram[0],brightness)};
    if(native_landscape_scene(frame)) result.before_models=scenery_.prepare(frame,result.before_models);
    if(policy.solid_frontend_margins) result.clear=right_margin(result);
    return result;
}
} // namespace starfox::platform::nintendo_3ds

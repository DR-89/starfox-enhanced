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
    else if(native_panorama_scene(frame)) {
        for(const auto& source_pass:back) {
            const auto space=source_pass.layer==PpuLayer::bg2?PicaSpace::scenery:PicaSpace::screen;
            auto& groups=result.before_model_groups;
            if(groups.empty() || groups.back().space!=space) {
                PpuBatch group;group.space=space;group.expand_horizontal=true;
                groups.push_back(std::move(group));
            }
            groups.back().passes.push_back(source_pass);
        }
    }
    return result;
}
std::array<PpuRasterWork,2> GameLayers::work() const noexcept {
    auto before=retired_before_work_;
    const auto add=[&](PpuRasterWork part){before.decodes+=part.decodes;before.colour_updates+=part.colour_updates;};
    add(before_.work());for(const auto& group:panorama_groups_) add(group->work());
    return {before,after_.work()};
}
GameLayerFrames GameLayers::prepare(const GamePresentation& frame) {
    const auto policy=game_layer_plan(frame);
    const auto brightness=frame.raster->brightness;
    const auto retire=[&](PpuRasterWork part) {
        retired_before_work_.decodes+=part.decodes;retired_before_work_.colour_updates+=part.colour_updates;
    };
    PicaFrame before;
    if(policy.before_model_groups.empty()) {
        for(const auto& group:panorama_groups_) retire(group->work());
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
        while(panorama_groups_.size()>policy.before_model_groups.size()) {
            retire(panorama_groups_.back()->work());panorama_groups_.pop_back();
        }
        while(panorama_groups_.size()<policy.before_model_groups.size())
            panorama_groups_.push_back(std::make_unique<PicaRaster>());
        auto& groups=working_groups_;groups.clear();groups.reserve(panorama_groups_.size());
        // Strip redundant A8 only from isolated BG2 descriptors. The decoder's
        // owned mask still validates source ownership; mixed screen groups
        // retain provenance for colour math. No texture pixels are copied.
        auto& bg_images=working_bg_images_;bg_images.clear();bg_images.reserve(panorama_groups_.size()*pica_raster_max_strips);
        for(unsigned i=0;i<panorama_groups_.size();++i) {
            auto prepared=panorama_groups_[i]->prepare(frame.raster->ppu,policy.before_model_groups[i],
                frame.plan,brightness,frame.current->background_colour_subtract,pica_raster_base_guard,true);
            if(policy.before_model_groups[i].space==PicaSpace::scenery && !prepared.textures.empty()) {
                const auto first=bg_images.size();
                for(auto image:prepared.textures) {
                    image.source_layers={};image.layer_pitch=0;bg_images.push_back(image);
                }
                prepared.textures=std::span<const PicaImage>(bg_images).subspan(first,prepared.textures.size());
            }
            groups.push_back(prepared);
        }
        before=panorama_.prepare_layers(frame.plan,groups);
    }
    GameLayerFrames result{before,
        after_.prepare(frame.raster->ppu,policy.after_models,frame.plan,brightness,frame.current->background_colour_subtract),
        backdrop(frame.raster->ppu->cgram[0],brightness)};
    if(native_landscape_scene(frame)) result.before_models=scenery_.prepare(frame,result.before_models);
    if(policy.solid_frontend_margins) result.clear=right_margin(result);
    return result;
}
} // namespace starfox::platform::nintendo_3ds

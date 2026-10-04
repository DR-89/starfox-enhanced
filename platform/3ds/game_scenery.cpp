#include "starfox/platform/nintendo_3ds/game_scenery.hpp"
#include "starfox/platform/nintendo_3ds/raster_coverage.hpp"
#include "starfox/platform/nintendo_3ds/pica_composite.hpp"

namespace starfox::platform::nintendo_3ds {
bool native_landscape_scene(const GamePresentation& source) noexcept {
    if(!source.current || !source.raster || !source.raster->ppu) return false;
    const auto& s=*source.current;
    return (s.flow==simulation::GameFlowState::gameplay || s.flow==simulation::GameFlowState::training)
        && source.raster->ppu->background_mode==2 && !source.raster->ppu->tunnel_scene
        && s.background_landscape && s.landscape_grid_height<0;
}
LandscapePlane source_landscape_plane(const GamePresentation& source) {
    if(!native_landscape_scene(source)) throw std::invalid_argument("Not a native 3DS landscape scene");
    const auto& scene=*source.current;const auto& ppu=*source.raster->ppu;
    const auto scroll=scene.background_scroll_override.value_or(std::array{ppu.bg2_scroll_x,ppu.bg2_scroll_y});
    double intercept=scroll[1],slope=0,count=0,sx=0,sy=0,sxx=0,sxy=0;
    int last_raw=0,unwrapped=0;bool previous=false;
    if(ppu.bg2_vertical_offsets_enabled) for(unsigned i=0;i<32;++i) {
        const auto at=(0x2fa0+i)*2;
        const unsigned word=unsigned(ppu.vram[at])|(unsigned(ppu.vram[at+1])<<8);
        if(!(word&0x4000)) continue;
        const int raw=word&0x1fff;
        if(previous) {int delta=(raw-last_raw)&0x1fff;if(delta>4095) delta-=8192;unwrapped+=delta;}
        else unwrapped=raw;
        previous=true;last_raw=raw;
        const double x=i+1,y=unwrapped;++count;sx+=x;sy+=y;sxx+=x*x;sxy+=x*y;
    }
    if(count) {
        const double denominator=count*sxx-sx*sx;
        slope=count>1 && denominator!=0?(count*sxy-sx*sy)/denominator:0;
        intercept=(sy-slope*sx)/count;
    }
    // Exactly the same all-sample line as expanded source BG2. In particular,
    // do not fit a single edge step: the cartridge roll tables are quantized.
    const double offset=intercept+slope*(128+(int(scroll[0])&7))/8.;
    const unsigned period=((ppu.bg2_screen_size&2)?64:32)*(ppu.bg2_tile_size_16?16:8);
    double horizon=double(scene.landscape_atlas_origin)+112-offset+8;
    horizon-=std::round((horizon-120)/period)*period;
    return {horizon,-slope/8.,-double(scene.landscape_grid_height)};
}
PicaFrame GameScenery::prepare(const GamePresentation& source,const PicaFrame& bg2) {
    const auto plane=source_landscape_plane(source);
    if(!same_pica_plan(source.plan,bg2.plan)) throw std::invalid_argument("3DS terrain belongs to another source eye plan");
    if(bg2.draws.empty()) {vertices_.clear();return {source.plan,{},{},{},bg2.clear};}
    if(bg2.draws.size()!=bg2.textures.size() || bg2.draws.size()>pica_raster_max_strips
        || bg2.vertices.size()!=bg2.draws.size()*6)
        throw std::invalid_argument("3DS terrain requires an isolated guarded BG2 source raster");
    // Isolated BG2 is identified by its source painter contract, not palette
    // colors or model rectangles. Retain opaque black. Never re-walk its
    // complete provenance image for every slider/eye presentation.
    const double focal_x=source.plan.focal_x,focal_y=source.plan.focal_y;
    const double distance_scale=plane.height*focal_y;
    const auto guard=pica_receiver_guard(source.plan,{-plane.slope/distance_scale,1/distance_scale,
        (200*plane.slope-plane.centre)/distance_scale});
    const double coverage_left=bg2.vertices.front().position[0];
    // Last vertex of each source quad is its left-bottom corner, not right.
    const double right=bg2.vertices[bg2.vertices.size()-4].position[0];
    if(coverage_left> -double(guard) || right<top_width+double(guard))
        throw std::invalid_argument("3DS terrain raster does not cover both eye receivers");
    using Point=std::array<double,2>;
    const auto distance=[&](Point point) {return point[1]-plane.centre-plane.slope*(point[0]-200);};
    const auto clip=[&](std::vector<Point>& polygon,double limit,bool above) {
        auto old=std::move(polygon);polygon.clear();
        if(old.empty()) return;
        auto a=old.back();double da=distance(a)-limit;
        for(const auto b:old) {
            const double db=distance(b)-limit;
            const bool inside_a=above?da>=0:da<=0,inside_b=above?db>=0:db<=0;
            if(inside_a!=inside_b) {
                const double t=da/(da-db);
                polygon.push_back({std::lerp(a[0],b[0],t),std::lerp(a[1],b[1],t)});
            }
            if(inside_b) polygon.push_back(b);
            a=b;da=db;
        }
    };
    std::vector<PicaVertex> next(bg2.vertices.begin(),bg2.vertices.end());
    std::vector<PicaImage> images(bg2.textures.begin(),bg2.textures.end());
    std::vector<PicaDraw> draws(bg2.draws.begin(),bg2.draws.end());
    double previous=coverage_left;
    for(unsigned strip=0;strip<images.size();++strip) {
        auto& image=images[strip];auto& sky=draws[strip];
        const double left=bg2.vertices[strip*6].position[0],end=left+image.width;
        if(sky.first!=strip*6 || sky.count!=6 || sky.texture!=strip || sky.space!=PicaSpace::scenery
            || sky.source_layer!=2 || image.height!=screen_height || image.channels!=4
            || image.repeat || image.source_layers.empty() || left!=previous)
            throw std::invalid_argument("Invalid 3DS source receiver strip");
        static_cast<void>(pica_texture_layout(image));previous=end;
        image.source_layers={};image.layer_pitch=0;sky.alpha_blend=false;
    }
    // All infinity strips precede every finite strip: interleaving would let
    // a later no-depth sky overwrite an earlier receiver after eye parallax.
    for(unsigned strip=0;strip<images.size();++strip) {
        const auto& image=images[strip];const double left=bg2.vertices[strip*6].position[0],end=left+image.width;
        std::vector<Point> polygon{{left,0},{end,0},{end,240},{left,240}};
        clip(polygon,distance_scale/source.plan.far_plane,true);
        clip(polygon,distance_scale/source.plan.near_plane,false);
        const unsigned first=unsigned(next.size());
        for(unsigned corner=1;corner+1<polygon.size();++corner) for(unsigned i:{0U,corner,corner+1}) {
            const auto point=polygon[i];const double z=distance_scale/distance(point);
            next.push_back({{float((point[0]-200)*z/focal_x),float((120-point[1])*z/focal_y),float(z)},
                {1,1,1,1},{float(std::clamp((point[0]-left)/image.width,0.,1.)),float(std::clamp(point[1]/screen_height,0.,1.))}});
        }
        if(next.size()>first) {
            PicaDraw ground;ground.first=first;ground.count=unsigned(next.size())-first;ground.texture=strip;
            ground.source_layer=2;ground.projected_uv=true;draws.push_back(ground);
        }
    }
    PicaFrame result{source.plan,next,draws,images,bg2.clear};validate_pica_group(result,512*256*4);
    vertices_=std::move(next);images_=std::move(images);draws_=std::move(draws);
    return {source.plan,vertices_,draws_,images_,bg2.clear};
}
} // namespace starfox::platform::nintendo_3ds

#include "starfox/platform/nintendo_3ds/game_scenery.hpp"
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
    if(bg2.draws.size()!=1 || bg2.textures.size()!=1 || bg2.vertices.size()!=6
        || bg2.draws[0].texture!=0 || bg2.draws[0].space!=PicaSpace::scenery || bg2.draws[0].source_layer!=2
        || bg2.textures[0].width!=top_width+64 || bg2.textures[0].height!=screen_height
        || bg2.textures[0].channels!=4 || bg2.textures[0].repeat
        || bg2.textures[0].source_layers.empty())
        throw std::invalid_argument("3DS terrain requires an isolated guarded BG2 source raster");
    // Isolated BG2 is identified by its source painter contract, not palette
    // colors or model rectangles. Retain opaque black. Never re-walk its
    // complete provenance image for every slider/eye presentation.
    const auto& art=bg2.textures[0];
    for(unsigned eye=0;eye<source.plan.eye_count;++eye) {
        static_cast<void>(PicaProjection(source.plan,eye));
        if(std::abs(background_offset(source.plan,eye))>32)
            throw std::invalid_argument("3DS scenery exceeds source guard coverage");
    }
    const double focal_x=source.plan.focal_x,focal_y=source.plan.focal_y;
    using Point=std::array<double,2>;
    std::vector<Point> polygon{{-32,0},{432,0},{432,240},{-32,240}};
    const auto distance=[&](Point point) {return point[1]-plane.centre-plane.slope*(point[0]-200);};
    const auto clip=[&](double limit,bool above) {
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
    // The far interval is retained by the same infinite BG2 artwork beneath
    // the finite receiver, not filled with an invented flat color or sky.
    clip(plane.height*focal_y/source.plan.far_plane,true);
    clip(plane.height*focal_y/source.plan.near_plane,false);
    std::vector<PicaVertex> next(bg2.vertices.begin(),bg2.vertices.end());
    const auto vertex=[&](Point point) {
        const double z=plane.height*focal_y/distance(point);
        return PicaVertex{{float((point[0]-200)*z/focal_x),float((120-point[1])*z/focal_y),float(z)},
            {1,1,1,1},{float(std::clamp((point[0]+32)/art.width,0.,1.)),float(std::clamp(point[1]/screen_height,0.,1.))}};
    };
    for(unsigned corner=1;corner+1<polygon.size();++corner)
        for(unsigned i:{0U,corner,corner+1}) next.push_back(vertex(polygon[i]));
    auto image=art;image.source_layers={};image.layer_pitch=0; // BG2 one-hot draw replaces mixed-layer A8.
    auto sky=bg2.draws[0];sky.space=PicaSpace::scenery;sky.source_layer=2;sky.alpha_blend=false;
    PicaDraw ground;ground.first=6;ground.count=unsigned(next.size()-6);ground.texture=0;
    ground.source_layer=2;ground.projected_uv=true;
    vertices_=std::move(next);image_[0]=image;draws_={sky,ground};
    return {source.plan,vertices_,std::span(draws_).first(ground.count?2:1),image_,bg2.clear};
}
} // namespace starfox::platform::nintendo_3ds

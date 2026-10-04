#include "starfox/platform/nintendo_3ds/game_scenery.hpp"
#include "starfox/platform/nintendo_3ds/raster_coverage.hpp"
#include "starfox/platform/nintendo_3ds/pica_composite.hpp"

namespace starfox::platform::nintendo_3ds {
bool native_corridor_scene(const GamePresentation& source) noexcept {
    if(!source.current || !source.raster || !source.raster->ppu || source.raster->boss_roll
        || !source.raster->ppu->tunnel_scene || source.raster->ppu->background_mode<1
        || source.raster->ppu->background_mode>2) return false;
    const auto& scene=*source.current;
    if(!scene.background_corridor) return false;
    const auto& box=*scene.background_corridor;
    // An exit camera outside the authored tube is a different surround policy,
    // not a negative-height plane or a reason to silently clamp stereo optics.
    if(scene.camera.x<=box.left || scene.camera.x>=box.right
        || scene.camera.y<=box.top || scene.camera.y>=box.bottom) return false;
    using enum simulation::GameFlowState;
    return scene.flow==gameplay || scene.flow==training || scene.flow==intro || scene.flow==planet_travel
        || scene.flow==stage_results || scene.flow==game_over || scene.flow==finished || scene.flow==credits;
}
std::array<std::array<double,3>,4> source_corridor_planes(const GamePresentation& source) {
    if(!native_corridor_scene(source) || !std::isfinite(source.interpolation_alpha))
        throw std::invalid_argument("Invalid native 3DS corridor source");
    const auto& now=*source.current;const auto box=*now.background_corridor;
    double alpha=1;
    if(source.previous && source.previous->flow==now.flow && source.previous->scene_epoch==now.scene_epoch
        && source.previous->background_id==now.background_id && source.previous->background_corridor==now.background_corridor
        && !timing::camera_transform_is_discontinuous(source.previous->camera,now.camera)
        && source.previous->camera.x>box.left && source.previous->camera.x<box.right
        && source.previous->camera.y>box.top && source.previous->camera.y<box.bottom)
        alpha=std::clamp(source.interpolation_alpha,0.,1.);
    const auto camera=source.previous?timing::interpolate(source.previous->camera,now.camera,alpha)
        :timing::interpolate(now.camera,now.camera,1);
    const auto view=source.previous?simulation::interpolate_rotation_matrix_q15(source.previous->view_matrix,now.view_matrix,alpha)
        :now.view_matrix;
    std::array<std::array<double,6>,3> inverse{};
    for(unsigned row=0;row<3;++row) {
        for(unsigned col=0;col<3;++col) inverse[row][col]=double(view[col*3+row])/32768.;
        inverse[row][row+3]=1;
    }
    for(unsigned col=0;col<3;++col) {
        unsigned pivot=col;
        for(unsigned row=col+1;row<3;++row) if(std::abs(inverse[row][col])>std::abs(inverse[pivot][col])) pivot=row;
        if(std::abs(inverse[pivot][col])<1.e-9) throw std::invalid_argument("Singular source corridor camera matrix");
        std::swap(inverse[pivot],inverse[col]);const double divisor=inverse[col][col];
        for(auto& value:inverse[col]) value/=divisor;
        for(unsigned row=0;row<3;++row) if(row!=col) {
            const double factor=inverse[row][col];
            for(unsigned k=0;k<6;++k) inverse[row][k]-=factor*inverse[col][k];
        }
    }
    const std::array distances{camera.x-box.left,box.right-camera.x,camera.y-box.top,box.bottom-camera.y};
    std::array<std::array<double,3>,4> result{};
    for(unsigned wall=0;wall<4;++wall) {
        const unsigned axis=wall/2;const double sign=(wall&1)?1.:-1.,d=distances[wall];
        const std::array normal{sign*inverse[axis][3],-sign*inverse[axis][4],sign*inverse[axis][5]};
        result[wall]={normal[0]/(d*source.plan.focal_x),-normal[1]/(d*source.plan.focal_y),
            (normal[2]-200*normal[0]/source.plan.focal_x+120*normal[1]/source.plan.focal_y)/d};
    }
    return result;
}
unsigned source_corridor_guard(const GamePresentation& source) {
    static_cast<void>(source_corridor_planes(source));
    // Source tunnel margins clamp one authored cross-section's edge pixels.
    // Their finite geometry can extend analytically without allocating tens
    // of thousands of identical decoded columns at large stereo separation.
    return pica_scenery_guard(source.plan);
}
bool native_landscape_scene(const GamePresentation& source) noexcept {
    if(!source.current || !source.raster || !source.raster->ppu) return false;
    const auto& s=*source.current;
    return (s.flow==simulation::GameFlowState::gameplay || s.flow==simulation::GameFlowState::training)
        && source.raster->ppu->background_mode==2 && !source.raster->ppu->tunnel_scene
        && s.background_landscape && s.landscape_grid_height<0;
}
bool native_water_scene(const GamePresentation& source) noexcept {
    if(!source.current || !source.raster || !source.raster->ppu || source.raster->boss_roll) return false;
    const auto& scene=*source.current;const auto& ppu=*source.raster->ppu;
    using enum simulation::GameFlowState;
    return scene.background_water_surround && ppu.background_mode==1 && !ppu.tunnel_scene
        && (scene.flow==gameplay || scene.flow==training || scene.flow==intro || scene.flow==planet_travel
            || scene.flow==stage_results || scene.flow==game_over || scene.flow==finished || scene.flow==credits);
}
double source_water_height(const GamePresentation& source) {
    if(!native_water_scene(source) || !std::isfinite(source.interpolation_alpha))
        throw std::invalid_argument("Invalid native 3DS water source");
    const auto height=[](const auto& scene){return std::abs(double(scene.camera.y)-scene.shadow_height);};
    const auto& now=*source.current;double result=height(now);
    if(source.previous && source.previous->background_water_surround && source.previous->flow==now.flow
        && height(*source.previous)>0 && !timing::camera_transform_is_discontinuous(source.previous->camera,now.camera))
        result=std::lerp(height(*source.previous),result,std::clamp(source.interpolation_alpha,0.,1.));
    if(result<=0) throw std::invalid_argument("3DS water camera lies on its receiver plane");
    return result;
}
unsigned source_water_guard(const GamePresentation& source) {
    const double distance=source_water_height(source)*source.plan.focal_y;
    return std::max(pica_receiver_guard(source.plan,{0,1/distance,-120/distance}),
        pica_receiver_guard(source.plan,{0,-1/distance,120/distance}));
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
PicaFrame GameScenery::prepare_water(const GamePresentation& source,const PicaFrame& bg2,unsigned available_guard) {
    const double height=source_water_height(source),distance=height*source.plan.focal_y;
    if(!same_pica_plan(source.plan,bg2.plan) || available_guard<source_water_guard(source))
        throw std::invalid_argument("3DS water source does not cover both eye receivers");
    if(bg2.draws.size()!=bg2.textures.size() || bg2.draws.size()>pica_raster_max_strips
        || bg2.vertices.size()!=bg2.draws.size()*6)
        throw std::invalid_argument("3DS water requires isolated BG2 source strips");
    using Point=std::array<double,2>;
    const auto clip=[](std::vector<Point>& polygon,double y,bool below) {
        auto old=std::move(polygon);polygon.clear();if(old.empty()) return;
        auto a=old.back();double da=a[1]-y;
        for(const auto b:old) {
            const double db=b[1]-y;const bool ia=below?da>=0:da<=0,ib=below?db>=0:db<=0;
            if(ia!=ib) {
                const double t=da/(da-db);
                polygon.push_back({std::lerp(a[0],b[0],t),y});
            }
            if(ib) polygon.push_back(b);
            a=b;da=db;
        }
    };
    std::vector<PicaVertex> vertices;std::vector<PicaDraw> draws;
    std::vector<PicaImage> images(bg2.textures.begin(),bg2.textures.end());
    for(unsigned i=0;i<images.size();++i) {
        auto& image=images[i];const auto& draw=bg2.draws[i];
        if(draw.first!=i*6 || draw.count!=6 || draw.texture!=i || draw.source_layer!=2
            || draw.space!=PicaSpace::scenery || image.channels!=4 || image.repeat || image.source_layers.empty())
            throw std::invalid_argument("Invalid isolated 3DS water artwork");
        static_cast<void>(pica_texture_layout(image));image.source_layers={};image.layer_pitch=0;
    }
    // Only the subpixel far-horizon band remains at infinity. Never leave a
    // complete old planar water/bridge image beneath the finite surfaces.
    for(int side:{0,-1,1}) for(unsigned strip=0;strip<images.size();++strip) {
        const auto& image=images[strip];const auto& origin=bg2.vertices[strip*6].position;
        const double left=origin[0],top=origin[1],right=left+image.width,bottom=top+image.height;
        std::vector<Point> polygon{{left,top},{right,top},{right,bottom},{left,bottom}};
        if(side==0) {
            clip(polygon,120-distance/source.plan.far_plane,true);
            clip(polygon,120+distance/source.plan.far_plane,false);
        } else {
            clip(polygon,120+side*distance/source.plan.far_plane,side>0);
            clip(polygon,120+side*distance/source.plan.near_plane,side<0);
        }
        const unsigned first=unsigned(vertices.size());
        for(unsigned corner=1;corner+1<polygon.size();++corner) for(unsigned k:{0U,corner,corner+1}) {
            const auto point=polygon[k];const double z=side?distance/(side*(point[1]-120)):0;
            const Point3 position=side?Point3{float((point[0]-200)*z/source.plan.focal_x),
                float((120-point[1])*z/source.plan.focal_y),float(z)}:Point3{float(point[0]),float(point[1]),0};
            vertices.push_back({position,{1,1,1,1},
                {float(std::clamp((point[0]-left)/image.width,0.,1.)),float(std::clamp((point[1]-top)/image.height,0.,1.))}});
        }
        if(vertices.size()!=first) {
            PicaDraw draw;draw.first=first;draw.count=unsigned(vertices.size())-first;draw.texture=strip;
            draw.source_layer=2;draw.space=side?PicaSpace::world:PicaSpace::scenery;
            draw.projected_uv=side!=0;draw.depth_test=draw.depth_write=side!=0;draws.push_back(draw);
        }
    }
    validate_pica_group({source.plan,vertices,draws,images,bg2.clear},512U*256U*4U);
    vertices_=std::move(vertices);draws_=std::move(draws);images_=std::move(images);
    return {source.plan,vertices_,draws_,images_,bg2.clear};
}
PicaFrame GameScenery::prepare_corridor(const GamePresentation& source,const PicaFrame& bg2,unsigned available_guard) {
    const auto planes=source_corridor_planes(source);
    if(!same_pica_plan(source.plan,bg2.plan) || available_guard<source_corridor_guard(source))
        throw std::invalid_argument("3DS corridor source does not cover both eyes");
    if(bg2.draws.size()!=bg2.textures.size() || bg2.draws.size()>pica_raster_max_strips
        || bg2.vertices.size()!=bg2.draws.size()*6)
        throw std::invalid_argument("3DS corridor requires isolated BG2 strips");
    using Point=std::array<double,2>;using Plane=std::array<double,3>;
    const auto depth=[](Point p,Plane q){return q[0]*p[0]+q[1]*p[1]+q[2];};
    const auto clip=[&](std::vector<Point>& polygon,Plane q) {
        auto old=std::move(polygon);polygon.clear();if(old.empty()) return;
        auto a=old.back();double da=depth(a,q);
        for(const auto b:old) {
            const double db=depth(b,q);
            if((da>=0)!=(db>=0)) {
                const double t=da/(da-db);polygon.push_back({std::lerp(a[0],b[0],t),std::lerp(a[1],b[1],t)});
            }
            if(db>=0) polygon.push_back(b);
            a=b;da=db;
        }
    };
    std::vector<PicaVertex> vertices;std::vector<PicaDraw> draws;
    std::vector<PicaImage> images(bg2.textures.begin(),bg2.textures.end());
    for(unsigned i=0;i<images.size();++i) {
        auto& image=images[i];const auto& draw=bg2.draws[i];
        if(draw.first!=i*6 || draw.count!=6 || draw.texture!=i || draw.source_layer!=2
            || draw.space!=PicaSpace::scenery || image.channels!=4 || image.repeat || image.source_layers.empty())
            throw std::invalid_argument("Invalid isolated 3DS corridor artwork");
        static_cast<void>(pica_texture_layout(image));image.source_layers={};image.layer_pitch=0;
    }
    // Partition source rays by their first tube intersection. A single flat
    // image, four unbounded overlapping planes, or a floor/ceiling-only mesh
    // gives incorrect side-wall depth and changes native pixels at the corners.
    // Only the tiny region beyond far remains infinity, before every receiver.
    double mesh_guard=available_guard;
    for(unsigned eye=0;eye<source.plan.eye_count;++eye)
        mesh_guard=std::max(mesh_guard,std::abs(double(source.plan.eyes[eye].projection_offset))
            +std::abs(double(source.plan.focal_x)*source.plan.eyes[eye].x)/source.plan.near_plane);
    for(unsigned surface=0;surface<5;++surface) for(unsigned strip=0;strip<images.size();++strip) for(int edge:{-1,0,1}) {
        const auto& image=images[strip];const auto& origin=bg2.vertices[strip*6].position;
        const double left=origin[0],top=origin[1],right=left+image.width;
        if(edge<0 && (strip!=0 || left> -double(available_guard))) continue;
        if(edge>0 && (strip+1!=images.size() || right<top_width+double(available_guard))) continue;
        const double begin=edge<0?-mesh_guard:edge>0?right:left,end=edge<0?left:edge>0?top_width+mesh_guard:right;
        if(begin>=end) continue;
        std::vector<Point> polygon{{begin,top},{end,top},{end,top+image.height},{begin,top+image.height}};
        if(surface==0) for(const auto q:planes) clip(polygon,{-q[0],-q[1],1./source.plan.far_plane-q[2]});
        else {
            const auto q=planes[surface-1];
            for(unsigned other=0;other<planes.size();++other) if(other!=surface-1)
                clip(polygon,{q[0]-planes[other][0],q[1]-planes[other][1],q[2]-planes[other][2]});
            clip(polygon,{q[0],q[1],q[2]-1./source.plan.far_plane});
            clip(polygon,{-q[0],-q[1],1./source.plan.near_plane-q[2]});
        }
        const unsigned first=unsigned(vertices.size());
        for(unsigned corner=1;corner+1<polygon.size();++corner) for(unsigned k:{0U,corner,corner+1}) {
            const auto point=polygon[k];const double z=surface?1./depth(point,planes[surface-1]):0;
            const Point3 position=surface?Point3{float((point[0]-200)*z/source.plan.focal_x),
                float((120-point[1])*z/source.plan.focal_y),float(z)}:Point3{float(point[0]),float(point[1]),0};
            vertices.push_back({position,{1,1,1,1},
                {float(std::clamp((point[0]-left)/image.width,0.,1.)),float(std::clamp((point[1]-top)/image.height,0.,1.))}});
        }
        if(vertices.size()!=first) {
            PicaDraw draw;draw.first=first;draw.count=unsigned(vertices.size())-first;draw.texture=strip;draw.source_layer=2;
            draw.space=surface?PicaSpace::world:PicaSpace::scenery;draw.projected_uv=surface!=0;
            draw.depth_test=draw.depth_write=surface!=0;draws.push_back(draw);
        }
    }
    validate_pica_group({source.plan,vertices,draws,images,bg2.clear},512U*256U*4U);
    vertices_=std::move(vertices);draws_=std::move(draws);images_=std::move(images);
    return {source.plan,vertices_,draws_,images_,bg2.clear};
}
} // namespace starfox::platform::nintendo_3ds

#pragma once
#include "starfox/platform/nintendo_3ds/pica_frame.hpp"

namespace starfox::platform::nintendo_3ds {
inline constexpr unsigned pica_raster_base_guard=32,pica_raster_strip_width=1024;
inline constexpr unsigned pica_raster_max_width=4096,pica_raster_max_strips=4;

// Coverage is derived from the immutable eye plan, never by reducing the
// requested separation/strength. Texture descriptors borrow horizontal strips
// of a shared decoded raster, respecting PICA's 1024-texel sampler limit.
inline unsigned pica_scenery_guard(const FramePlan& plan) {
    double extent=pica_raster_base_guard;
    for(unsigned eye=0;eye<plan.eye_count;++eye) {
        static_cast<void>(PicaProjection(plan,eye));
        extent=std::max(extent,std::abs(double(background_offset(plan,eye))));
    }
    if(plan.eye_count!=(plan.stereo?2U:1U) || !std::isfinite(extent)
        || extent>(pica_raster_max_width-top_width)/2)
        throw std::invalid_argument("3DS scenery eye coverage exceeds raster storage");
    return (unsigned(std::ceil(extent))+7U)&~7U;
}

// A mono source receiver has reciprocal depth q=a*x+b*y+c. Intersect the
// visible eye frustum with near/far first, then solve back to the source
// pixel. This includes finite parallax, not just the infinity shift.
inline unsigned pica_receiver_guard(const FramePlan& plan,std::array<double,3> q) {
    double extent=pica_scenery_guard(plan);
    for(double value:q) if(!std::isfinite(value))
        throw std::invalid_argument("Non-finite 3DS receiver plane");
    using Point=std::array<double,2>;
    for(unsigned eye=0;eye<plan.eye_count;++eye) {
        const double motion=double(plan.focal_x)*plan.eyes[eye].x;
        const double offset=background_offset(plan,eye),divisor=1-motion*q[0];
        if(std::abs(divisor)<1.e-10)
            throw std::invalid_argument("3DS eye lies on source receiver plane");
        const auto source_x=[&](Point p){return (p[0]-offset+motion*(q[1]*p[1]+q[2]))/divisor;};
        const auto depth=[&](Point p){return q[0]*source_x(p)+q[1]*p[1]+q[2];};
        std::vector<Point> polygon{{0,0},{400,0},{400,240},{0,240}};
        for(unsigned side=0;side<2;++side) {
            const double limit=side?1./plan.near_plane:1./plan.far_plane;
            auto old=std::move(polygon);polygon.clear();if(old.empty()) break;
            auto a=old.back();double da=depth(a)-limit;
            for(auto b:old) {
                const double db=depth(b)-limit;
                const bool ia=side?da<=0:da>=0,ib=side?db<=0:db>=0;
                if(ia!=ib) {
                    const double t=da/(da-db);
                    polygon.push_back({std::lerp(a[0],b[0],t),std::lerp(a[1],b[1],t)});
                }
                if(ib) polygon.push_back(b);
                a=b;da=db;
            }
        }
        for(auto point:polygon) {
            const auto x=source_x(point);
            extent=std::max({extent,-x,x-top_width});
        }
    }
    if(!std::isfinite(extent) || extent>(pica_raster_max_width-top_width)/2)
        throw std::invalid_argument("3DS finite receiver exceeds raster storage");
    return (unsigned(std::ceil(extent))+7U)&~7U;
}
} // namespace starfox::platform::nintendo_3ds

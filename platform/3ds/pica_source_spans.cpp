#include "starfox/platform/nintendo_3ds/pica_source_spans.hpp"
#include "starfox/render/source_polygon_spans.hpp"
#include <map>
#include <tuple>

namespace starfox::platform::nintendo_3ds {
namespace {
using Camera=std::array<double,3>;
using Screen=std::array<double,2>;
template<class Point,class Distance>
std::vector<Point> clip(std::span<const Point> polygon,Distance distance) {
    std::vector<Point> out;if(polygon.empty()) return out;
    auto previous=polygon.back();auto before=distance(previous);
    for(const auto& current:polygon) {
        const auto after=distance(current);
        if((before<0)!=(after<0)) {
            const auto t=before/(before-after);Point intersection{};
            for(unsigned axis=0;axis<intersection.size();++axis)
                intersection[axis]=previous[axis]+t*(current[axis]-previous[axis]);
            out.push_back(intersection);
        }
        if(after>=0) out.push_back(current);
        previous=current;before=after;
    }
    return out;
}
std::vector<Camera> depth_clip(std::span<const Camera> polygon,const FramePlan& plan) {
    auto out=clip<Camera>(polygon,[&](Camera p){return p[2]-plan.near_plane;});
    return clip<Camera>(out,[&](Camera p){return plan.far_plane-p[2];});
}
struct Plane {
    Camera normal{};double distance{};
    std::vector<Screen> boundary;double winding{};
};
Camera difference(Camera a,Camera b) {return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
double dot(Camera a,Camera b) {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
Screen projected(Camera p,const render::RenderPose& pose,double focal) {
    return {pose.vanish_x+focal*p[0]/p[2],pose.vanish_y+focal*p[1]/p[2]};
}
double denominator(const Plane& plane,Screen p,const render::RenderPose& pose,double focal) {
    return plane.normal[0]*(p[0]-pose.vanish_x)/focal
        +plane.normal[1]*(p[1]-pose.vanish_y)/focal+plane.normal[2];
}
// Piecewise authored fan depth is retained for non-planar source boundaries.
// A source pixel touching a rounded outline uses its nearest original fan
// region, rather than flattening the whole face to an average depth.
unsigned region(std::span<const Plane> planes,Screen point,const render::RenderPose& pose,
    double focal,const FramePlan& plan) {
    unsigned selected=~0U;double best=-std::numeric_limits<double>::infinity();
    for(unsigned i=0;i<planes.size();++i) {
        const auto& plane=planes[i];const auto d=denominator(plane,point,pose,focal);
        if(d<=0) continue;
        const auto z=plane.distance/d;
        if(z<plan.near_plane-1.e-6 || z>plan.far_plane+1.e-6) continue;
        double score=std::numeric_limits<double>::infinity();
        for(unsigned j=0;j<plane.boundary.size();++j) {
            const auto a=plane.boundary[j],b=plane.boundary[(j+1)%plane.boundary.size()];
            const auto length=std::hypot(b[0]-a[0],b[1]-a[1]);
            if(length<=1.e-12) continue;
            const auto edge=(b[0]-a[0])*(point[1]-a[1])-(b[1]-a[1])*(point[0]-a[0]);
            score=std::min(score,plane.winding*edge/length);
        }
        if(score>best) {best=score;selected=i;}
    }
    return selected;
}
struct Rectangle {int left{},right{},top{},bottom{},delta{};unsigned plane{};};
}
std::vector<PicaVertex> pica_source_span_geometry(
    std::span<const render::ShapePrimitiveVertex> vertices,const render::RenderPose& pose,
    double focal,std::array<double,2> origin,const FramePlan& plan,
    std::array<float,4> colour,unsigned budget) {
    if(vertices.size()<3 || vertices.size()>128 || !std::isfinite(focal) || focal<=0
        || !std::isfinite(pose.vanish_x) || !std::isfinite(pose.vanish_y)
        || !std::isfinite(origin[0]) || !std::isfinite(origin[1]) || budget>pica_vertex_limit)
        throw std::invalid_argument("Invalid 3DS source span input");
    for(float channel:colour) if(!std::isfinite(channel) || channel<0 || channel>1)
        throw std::invalid_argument("Invalid 3DS source span colour");
    if(!plan.eye_count || plan.eye_count>2) throw std::invalid_argument("Invalid 3DS span eye plan");
    for(unsigned eye=0;eye<plan.eye_count;++eye) static_cast<void>(PicaProjection(plan,eye));
    std::vector<Camera> camera;camera.reserve(vertices.size());
    for(const auto& vertex:vertices) {
        for(double value:vertex.camera) if(!std::isfinite(value)
            || std::abs(value)>std::numeric_limits<float>::max())
            throw std::invalid_argument("Invalid 3DS source span camera vertex");
        camera.push_back(vertex.camera);
    }
    const auto visible=depth_clip(camera,plan);
    if(visible.size()<3) return {};
    double min_z=plan.far_plane,max_z=plan.near_plane;
    for(const auto& p:visible) {min_z=std::min(min_z,p[2]);max_z=std::max(max_z,p[2]);}
    double guard=0;
    // At every depth in this face, each eye's source-pixel displacement is
    // bounded by one of these endpoints. No mono-frustum crop or left-eye-only
    // choice can discard ink which remains visible in the other eye.
    for(unsigned eye=0;eye<plan.eye_count;++eye) for(double z:{min_z,max_z})
        guard=std::max(guard,std::abs(focal*plan.eyes[eye].x/z
            -focal*plan.eyes[eye].projection_offset/plan.focal_x));
    const auto half_x=top_width*.5*focal/plan.focal_x;
    const auto half_y=screen_height*.5*focal/plan.focal_y;
    const std::array<double,4> bounds{std::floor(origin[0]-half_x-guard-2),
        std::ceil(origin[0]+half_x+guard+2),std::floor(origin[1]-half_y-4),
        std::ceil(origin[1]+half_y+4)};
    for(double bound:bounds) if(!std::isfinite(bound) || std::abs(bound)>32767)
        throw std::length_error("3DS span preparation exceeds signed source-coordinate budget");
    std::vector<Screen> boundary;for(auto p:visible) boundary.push_back(projected(p,pose,focal));
    boundary=clip<Screen>(boundary,[&](Screen p){return p[0]-bounds[0];});
    boundary=clip<Screen>(boundary,[&](Screen p){return bounds[1]-p[0];});
    boundary=clip<Screen>(boundary,[&](Screen p){return p[1]-bounds[2];});
    boundary=clip<Screen>(boundary,[&](Screen p){return bounds[3]-p[1];});
    if(boundary.size()<3) return {};
    std::vector<Plane> planes;
    for(unsigned corner=1;corner+1<camera.size();++corner) {
        const auto a=difference(camera[corner],camera[0]),b=difference(camera[corner+1],camera[0]);
        Camera n{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
        const auto length=std::sqrt(dot(n,n));if(length<=1.e-12) continue;
        for(auto& value:n) value/=length;
        double d=dot(n,camera[0]);if(std::abs(d)<=1.e-12) continue;
        if(d<0) {d=-d;for(auto& value:n) value=-value;}
        const std::array<Camera,3> triangle{camera[0],camera[corner],camera[corner+1]};
        const auto clipped=depth_clip(triangle,plan);if(clipped.size()<3) continue;
        Plane plane;plane.normal=n;plane.distance=d;
        for(auto p:clipped) plane.boundary.push_back(projected(p,pose,focal));
        double area=0;
        for(unsigned i=0;i<plane.boundary.size();++i) {
            const auto p=plane.boundary[i],q=plane.boundary[(i+1)%plane.boundary.size()];
            area+=p[0]*q[1]-q[0]*p[1];
        }
        if(std::abs(area)<=1.e-12) continue;
        plane.winding=area<0?-1:1;planes.push_back(std::move(plane));
    }
    if(planes.empty()) return {}; // Genuine zero-area camera surfaces, not omitted source ink.
    bool coplanar=true;
    for(const auto& plane:planes) {
        for(unsigned axis=0;axis<3;++axis)
            coplanar&=std::abs(plane.normal[axis]-planes[0].normal[axis])<=1.e-9;
        coplanar&=std::abs(plane.distance-planes[0].distance)<=1.e-9*std::max(1.,planes[0].distance);
    }
    std::vector<render::SourceSpanPoint> points;
    for(auto p:boundary) points.push_back({int(std::lround(p[0])),int(std::lround(p[1]))});
    const render::SourceSpanModes modes{pose.wireframe_mode,pose.wobble_mode,pose.cel_mode,
        pose.wave_mode,true,pose.wave_offset,pose.animation_frame};
    std::vector<Rectangle> rectangles;
    using Key=std::tuple<int,int,int,unsigned>;
    std::map<Key,unsigned> active;
    const auto record=[&](int left,int right,int y,int delta,unsigned plane) {
        if(plane==~0U) return; // Ink outside all finite near/far-clipped source surfaces.
        const Key key{left,right,delta,plane};const auto old=active.find(key);
        if(old!=active.end()) {
            auto& rectangle=rectangles[old->second];
            if(y==rectangle.bottom || y==rectangle.bottom+1) {rectangle.bottom=y;return;}
        }
        if(rectangles.size()>=budget/6) throw std::length_error("3DS source span geometry budget exceeded");
        active[key]=unsigned(rectangles.size());rectangles.push_back({left,right,y,y,delta,plane});
    };
    unsigned work=0;
    render::source_polygon_spans(points,unsigned(std::max(225.,bounds[1]-bounds[0])),modes,
        [&](render::SourcePolygonSpan span) {
            if(span.left>span.right) throw std::logic_error("Invalid 3DS source span");
            const auto count=unsigned(span.right-span.left+1);
            if(count>4U*1024*1024-work) throw std::length_error("3DS source span preparation work budget exceeded");
            work+=count;
            const auto delta=span.y-span.source_y;
            if(coplanar) {record(span.left,span.right,span.y,delta,0);return;}
            int first=span.left;
            auto previous=region(planes,{first+.5,span.source_y+.5},pose,focal,plan);
            for(int x=first+1;x<=span.right;++x) {
                const auto next=region(planes,{x+.5,span.source_y+.5},pose,focal,plan);
                if(next!=previous) {record(first,x-1,span.y,delta,previous);first=x;previous=next;}
            }
            record(first,span.right,span.y,delta,previous);
        });
    std::vector<PicaVertex> result;result.reserve(std::min(budget,unsigned(rectangles.size()*6)));
    for(const auto& rectangle:rectangles) {
        const auto& plane=planes[rectangle.plane];
        const std::array<Screen,4> corners{{{double(rectangle.left),double(rectangle.top)},
            {double(rectangle.right+1),double(rectangle.top)},
            {double(rectangle.right+1),double(rectangle.bottom+1)},
            {double(rectangle.left),double(rectangle.bottom+1)}}};
        const auto ray=[&](Screen p){p[1]-=rectangle.delta;return denominator(plane,p,pose,focal);};
        // A one-pixel source footprint may straddle a physical near/far plane.
        // Clip that footprint analytically before reciprocal-depth division.
        auto polygon=clip<Screen>(corners,[&](Screen p){return ray(p)-plane.distance/plan.far_plane;});
        polygon=clip<Screen>(polygon,[&](Screen p){return plane.distance/plan.near_plane-ray(p);});
        if(polygon.size()<3) continue;
        std::vector<PicaVertex> native;
        for(auto p:polygon) {
            const auto z=plane.distance/ray(p);
            const Point3 position{float((p[0]-pose.vanish_x)*z/focal),
                float(-(p[1]-pose.vanish_y)*z/focal),float(z)};
            for(float value:position) if(!std::isfinite(value)) throw std::invalid_argument("Invalid 3DS source span depth");
            native.push_back({position,colour,{}});
        }
        const auto added=(native.size()-2)*3;
        if(added>budget-result.size()) throw std::length_error("3DS clipped source span geometry budget exceeded");
        for(unsigned corner=1;corner+1<native.size();++corner)
            for(unsigned index:{0U,corner,corner+1}) result.push_back(native[index]);
    }
    return result;
}
}

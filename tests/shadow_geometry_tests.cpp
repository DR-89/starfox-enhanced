#include "starfox/render/shadow_geometry.hpp"
#include "starfox/render/shadow_scene.hpp"
#include "starfox/render/shadow_mask.hpp"
#include <cstdlib>
#include <iostream>

void require(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    using namespace starfox::render::shadows;
    const Triangle face{{-2, 0, -2}, {2, 0, -2}, {0, 0, 2}};
    require(intersect({0, -5, 0}, {0, 1, 0}, face, .01, 10) == 5.0,
        "light ray must hit receiving geometry");
    require(intersect({0, -5, 0}, {0, 1, 0}, {face.c, face.b, face.a}, .01, 10) == 5.0,
        "shadow occlusion must be winding independent");
    require(!intersect({0, 0, 0}, {0, 1, 0}, face, .01, 10), "self-shadow bias");
    require(!intersect({4, -5, 0}, {0, 1, 0}, face, .01, 10), "outside triangle");
    require(!intersect({0, -5, 0}, {0, -1, 0}, face, .01, 10), "behind light ray");
    require(!intersect({0, -5, 0}, {0, 1, 0}, face, .01, 4), "finite light distance");
    require(!intersect({0, -5, 0}, {0, 1, 0}, {{}, {}, {}}, .01, 10), "degenerate face");
    const auto shadow = project_to_plane({0, -10, 0}, {1, 1, 0}, {}, {0, 1, 0});
    require(shadow && shadow->x == 10 && shadow->y == 0, "angled light projection");
    require(!project_to_plane({0, -10, 0}, {1, 0, 0}, {}, {0, 1, 0}), "parallel light");
    require(!project_to_plane({0, 10, 0}, {1, 1, 0}, {}, {0, 1, 0}), "receiver behind caster");
    Scene scene;
    std::vector<Triangle> reference;
    for (int z=-10;z<10;++z) for (int x=-10;x<10;++x) {
        const Vec3 offset{x*8.0,0,z*8.0};
        const Triangle triangle{face.a+offset,face.b+offset,face.c+offset};
        scene.add(triangle); reference.push_back(triangle);
    }
    scene.build();
    std::vector<Scene::GpuNode> packed_nodes;
    std::vector<Scene::GpuTriangle> packed_triangles;
    require(scene.pack_gpu(packed_nodes,packed_triangles),"built scene must export GPU hierarchy");
    require(packed_triangles.size()==scene.triangle_count(),"GPU triangle count");
    for(const auto& node:packed_nodes) {
        if(node.count) {
            require(std::size_t(node.begin)+node.count<=packed_triangles.size(),"GPU leaf range");
            for(unsigned i=node.begin;i<node.begin+node.count;++i) {
                const auto& triangle=scene.triangles()[i];
                for(const auto vertex:{triangle.a,triangle.b,triangle.c}) {
                    require(node.low[0]<=vertex.x && node.high[0]>=vertex.x
                        && node.low[1]<=vertex.y && node.high[1]>=vertex.y
                        && node.low[2]<=vertex.z && node.high[2]>=vertex.z,"GPU bounds must enclose source vertices");
                }
            }
        } else require(node.left<packed_nodes.size() && node.right<packed_nodes.size(),"GPU child indices");
    }
    const auto prepared_light=scene.prepare_direction({0,1,0});
    for (int x=-85;x<=85;++x) {
        const Vec3 origin{double(x),-5,0};
        bool expected=false;
        for (const auto& triangle:reference)
            expected |= intersect(origin,{0,1,0},triangle,.05,100).has_value();
        std::size_t tested=0;
        require(scene.occluded(origin,{0,1,0},.05,100,&tested)==expected,
            "accelerated shadow scene differs from all-triangle reference");
        require(scene.occluded(origin,{0,1,0},.05,100,nullptr,&prepared_light)==expected,
            "prepared light differs from uncached triangle intersections");
        require(scene.occluded(origin,{0,-1,0},.05,100,nullptr,&prepared_light)
                ==scene.occluded(origin,{0,-1,0},.05,100),
            "mismatched light direction used stale prepared terms");
        require(tested<reference.size()/4, "shadow lookup failed to prune distant geometry");
        for (double limit:{4.0,5.0,100.0}) {
            std::optional<double> closest;
            const Vec3 ray{.1,1,.25};
            for (const auto& triangle:reference) {
                const auto hit=intersect(origin,ray,triangle,.05,limit);
                if(hit && (!closest || *hit<*closest)) closest=hit;
            }
            require(scene.nearest(origin,ray,.05,limit)==closest,
                "near-first bounded traversal changed nearest receiver");
        }
    }
    scene.clear(); scene.build();
    require(scene.pack_gpu(packed_nodes,packed_triangles) && packed_nodes.empty()
        && packed_triangles.empty(),"GPU export must clear previous frame");
    require(!scene.occluded({0,-5,0},{0,1,0}), "scene retained previous frame casters");
    scene.add({{-10,-10,20},{10,-10,20},{0,10,40}});
    scene.add({{-10,-10,60},{10,-10,60},{0,10,60}});
    scene.build();
    require(scene.occluded({0,0,0},{0,1,0},.05,100,nullptr,&prepared_light)
        ==scene.occluded({0,0,0},{0,1,0},.05,100),
        "rebuilt scene reused stale prepared geometry");
    const auto near_surface=scene.nearest({}, {0,0,1});
    require(near_surface && std::abs(*near_surface-30)<1e-8,
        "receiver must follow slope and occlude the rear polygon");
    const auto lower_surface=scene.nearest({}, {0,-.2,1});
    require(lower_surface && *lower_surface<*near_surface,
        "sloping receiver incorrectly used one average face depth");
    scene.clear();
    scene.add({{-2,-2,20},{2,-2,20},{0,2,20}}); scene.build();
    std::vector<std::uint8_t> mask;
    const Camera camera{200,200,100,100,100};
    render_mask(scene,camera,{-1,0,-1},ReceiverPlane{{0,0,40},{0,0,1}},mask);
    require(mask[100*200+150]!=0, "angled light did not cast onto receiver");
    require(mask[100*200+100]==0, "caster incorrectly shadowed itself");
    require(mask[100*200+130]==0, "shadow leaked outside projected geometry");
    require(std::any_of(mask.begin(),mask.end(),[](auto value){return value>0 && value<160;}),
        "area light did not generate a partial-coverage penumbra");
    const auto serial_mask=mask;
    starfox::render::RowWorkers workers;
    workers.set_worker_count(4);
    render_mask(scene,camera,{-1,0,-1},ReceiverPlane{{0,0,40},{0,0,1}},mask,&workers);
    require(mask==serial_mask,"parallel shadows changed receiver pixels");
    render_mask(scene,camera,{-1,0,-1},std::nullopt,mask);
    require(mask[100*200+150]==0, "empty space received a shadow");
    // The foreground receiver is lit, while the plane hidden behind it is
    // shadowed by that same geometry. Removing casters would be incorrect.
    const Camera underlay_camera{1,1,1,.5,.5};
    const ReceiverPlane underlay_plane{{0,0,40},{0,0,1}};
    render_mask(scene,underlay_camera,{0,0,-1},underlay_plane,mask);
    require(mask==std::vector<std::uint8_t>{0},"foreground caster self-shadowed");
    render_mask(scene,underlay_camera,{0,0,-1},underlay_plane,mask,nullptr,false,true);
    require(mask==std::vector<std::uint8_t>{160},"underlay discarded foreground shadow caster");
    render_mask(scene,underlay_camera,{0,0,-1},{},mask,nullptr,false,true);
    require(mask==std::vector<std::uint8_t>{0},"underlay shadowed space without a plane");
    // A fixed angular emitter is not a fixed image-space blur: its projected
    // footprint must grow with the physical caster/receiver separation.
    // Examine the exposed half of a straight edge so primary visibility of
    // the caster cannot be mistaken for a contact shadow.
    for(unsigned quality:{1U,2U,3U}) {
        Camera contact_camera{128,8,1000,64,4};contact_camera.quality=quality;
        const auto penumbra=[&](double gap) {
            Scene edge;
            const double z=1000-gap;
            edge.add({{-1000,-1000,z},{0,-1000,z},{0,1000,z}});
            edge.add({{-1000,-1000,z},{0,1000,z},{-1000,1000,z}});
            edge.build();
            std::vector<std::uint8_t> result;
            render_mask(edge,contact_camera,{0,0,-1},ReceiverPlane{{0,0,1000},{0,0,1}},result);
            unsigned partial=0;
            for(unsigned x=64;x<128;++x) {
                const auto value=result[4*128+x];
                if(value>0 && value<160) ++partial;
            }
            return partial;
        };
        const auto contact=penumbra(1),near=penumbra(100),far=penumbra(500);
        require(contact==0,"contact shadow retained a fixed-width blur");
        require(near>contact && far>near,"shadow penumbra did not grow with caster separation");
    }
}

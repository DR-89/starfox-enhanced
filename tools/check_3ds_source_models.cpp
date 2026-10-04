#include "starfox/platform/nintendo_3ds/pica_shapes.hpp"
#include "starfox/assets/shape_decoder.hpp"
#include <iostream>
#include <map>
#include <set>

namespace {
using namespace starfox;
using namespace platform::nintendo_3ds;
unsigned checks{};
std::string context;
void require(bool value,const char* message) {
    ++checks;if(!value) throw std::runtime_error(context+": "+message);
}
}
int main(int argc,char** argv) try {
    if(argc!=3) throw std::invalid_argument("Usage: check_3ds_source_models ROM SYMBOLS (local, never bundled)");
    const auto rom=assets::RomImage::load(argv[1]);const auto symbols=assets::SymbolMap::load(argv[2]);
    assets::ShapeDecoder decoder(rom,symbols);render::SoftwareRenderer renderer;
    render::Palette256 colours;
    for(unsigned i=0;i<colours.size();++i) colours[i]={std::uint8_t(i),std::uint8_t(i^85),std::uint8_t(i^170),255};
    std::vector<std::uint8_t> lower(bottom_width*screen_height*3);
    const ImageView dashboard{lower,bottom_width,screen_height,bottom_width*3};
    std::set<std::uint32_t> addresses;
    std::map<std::uint32_t,std::string> candidates;
    for(const auto& [name,values]:symbols.entries()) for(auto address:values)
        if(addresses.insert(address).second && decoder.looks_like_shape_header(address)) candidates.emplace(address,name);
    unsigned models{},poses{},polygons{},lines{},sprites{},textured{},dithered{},bsp{},animated{},peak_vertices{},peak_draws{},peak_textures{};
    for(const auto& [address,name]:candidates) {
        context=name+" $"+std::to_string(address);
        const auto shape=decoder.decode(address,name);++models;
        bsp+=shape.bsp_root_address!=0;animated+=!shape.frames.empty();
        for(unsigned mode=0;mode<6;++mode) {
            context=name+" mode "+std::to_string(mode);
            render::RenderPose p;p.vanish_x=128;p.vanish_y=112;p.z=mode<2?768:2048;
            p.use_rotation_matrix=true;p.rotation_matrix={32767,0,0,0,32767,0,0,0,32767};
            p.animation_frame=shape.frames.empty()?mode:mode%shape.frames.size();p.colour_frame=mode;
            p.continuous_geometry=mode%2!=0;
            if(mode>=4) {p.explosion_progress=8;p.explosion_phase=mode==4?7.:7.5;}
            if(mode==3) {p.texture_scroll_x=13;p.texture_scroll_y=-9;p.colour_warp=true;}
            const auto prepared=renderer.prepare_primitives(shape,p);
            render::Framebuffer source(256,224);render::RenderDiagnostics trace;
            renderer.draw(shape,p,source,true,nullptr,nullptr,&trace);
            unsigned polygon=0;
            for(const auto& primitive:prepared.primitives) {
                if(primitive.kind==render::ShapePrimitiveKind::polygon) {
                    require(polygon<trace.polygons.size(),"Captured an extra source-visible polygon");
                    const auto& expected=trace.polygons[polygon++].camera;
                    require(primitive.vertices.size()==expected.size(),"Source polygon boundary was altered");
                    for(unsigned i=0;i<expected.size();++i)
                        require(primitive.vertices[i].camera==expected[i],"Source camera/explosion/animation geometry differs");
                    ++polygons;
                } else if(primitive.kind==render::ShapePrimitiveKind::line) ++lines;
                else if(primitive.kind==render::ShapePrimitiveKind::sprite) ++sprites;
                textured+=primitive.material.texture!=nullptr;dithered+=primitive.material.colour.dither;
            }
            require(polygon==trace.polygons.size(),"Captured geometry omitted a source-visible polygon");
            PicaShapes native;native.append(prepared,colours);
            for(float slider:{0.F,.5F,1.F}) {
                const auto frame=native.frame(plan_frame(slider,true,ScreenUse::world));
                validate_pica_frame(frame,dashboard);
                peak_vertices=std::max(peak_vertices,unsigned(frame.vertices.size()));
                peak_draws=std::max(peak_draws,unsigned(frame.draws.size()));
                peak_textures=std::max(peak_textures,unsigned(frame.textures.size()));
            }
            ++poses;
        }
    }
    require(models>100,"Cartridge fixture did not exercise a substantial source model catalogue");
    std::cout<<models<<" cartridge models / "<<poses<<" poses / "<<checks<<" camera-boundary checks\n"
        <<polygons<<" polygons, "<<lines<<" lines, "<<sprites<<" sprites, "<<textured<<" textured, "<<dithered<<" two-ink\n"
        <<bsp<<" BSP models, "<<animated<<" animated models; peak per-model "<<peak_vertices<<" vertices / "
        <<peak_draws<<" draws / "<<peak_textures<<" textures\n"
        <<"Source camera/BSP/material geometry and bounded native conversion; NOT native rendering or full-game acceptance\n";
} catch(const std::exception& error) {std::cerr<<context<<": "<<error.what()<<'\n';return 1;}

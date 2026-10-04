#include "starfox/platform/nintendo_3ds/pica_frame.hpp"
#include <iostream>

namespace {
using namespace starfox::platform::nintendo_3ds;
unsigned checks{};
void require(bool value,const char* message) {++checks;if(!value) throw std::runtime_error(message);}
template<class F> void rejects(F action,const char* message) {
    bool rejected=false;try {action();} catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,message);
}
void texture_upload() {
    // Independent Morton lookup, including vertical inversion and ABGR order.
    constexpr unsigned spread[]{0,1,4,5,16,17,20,21};
    for(unsigned channels:{3U,4U}) for(unsigned width:{1U,7U,8U,9U,31U,64U})
        for(unsigned height:{1U,7U,8U,17U,32U}) {
            const unsigned pitch=width*channels+7;
            std::vector<std::uint8_t> source(pitch*height,0xAD);
            for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x)
                for(unsigned c=0;c<channels;++c) source[y*pitch+x*channels+c]=std::uint8_t(x*17+y*29+c*53);
            const PicaImage image{source,width,height,pitch,channels};
            const auto layout=pica_texture_layout(image);
            require(layout.width>=8 && layout.height>=8 && layout.bytes==layout.width*layout.height*4,"Padded allocation");
            require(layout.uv_scale==std::array<float,2>{float(width)/layout.width,float(height)/layout.height},"Logical UV scale");
            std::vector<std::uint8_t> packed(layout.bytes+2,0xDD);
            pack_pica_texture(image,std::span(packed).subspan(1,layout.bytes));
            require(packed.front()==0xDD && packed.back()==0xDD,"Upload must not cross storage boundary");
            for(unsigned y=0;y<layout.height;++y) for(unsigned x=0;x<layout.width;++x) {
                const unsigned texture_y=layout.height-1-y;
                const unsigned tile=(texture_y/8)*(layout.width/8)+x/8;
                const unsigned offset=1+4*(tile*64+spread[x%8]+2*spread[texture_y%8]);
                const unsigned from=std::min(y,height-1)*pitch+std::min(x,width-1)*channels;
                require(packed[offset]==(channels==4?source[from+3]:255),"Alpha or RGB opacity");
                for(unsigned c=0;c<3;++c) require(packed[offset+3-c]==source[from+c],"Swizzle, row padding, edge fill and ABGR");
            }
            auto repeat=image;repeat.repeat=true;
            if(std::has_single_bit(width) && std::has_single_bit(height)) {
                pack_pica_texture(repeat,std::span(packed).subspan(1,layout.bytes));
                for(unsigned y=0;y<layout.height;++y) for(unsigned x=0;x<layout.width;++x) {
                    const unsigned ty=layout.height-1-y;
                    const unsigned offset=1+4*(((ty/8)*(layout.width/8)+x/8)*64+spread[x%8]+2*spread[ty%8]);
                    require(packed[offset+3]==source[(y%height)*pitch+(x%width)*channels],"Repeat padding wraps source artwork");
                }
            } else rejects([&]{pica_texture_layout(repeat);},"Non-power-of-two repeat rejected");
            rejects([&]{pack_pica_texture(image,std::span(packed).first(layout.bytes-1));},"Short upload rejected");
        }
    std::array<std::uint8_t,256> storage{};
    PicaImage image{storage,8,8,32,4};
    rejects([&]{pack_pica_texture(image,storage);},"Overlapping source cannot be swizzled in place");
    for(unsigned width:{0U,1025U,~0U}) {
        image.width=width;rejects([&]{pica_texture_layout(image);},"Invalid width rejected before arithmetic");
    }
    image={storage,8,8,32,4};image.pitch=31;
    rejects([&]{pica_texture_layout(image);},"Short pitch rejected");
    image={std::span(storage).first(255),8,8,32,4};
    rejects([&]{pica_texture_layout(image);},"Short logical rows rejected");
    image={storage,8,8,32,2};rejects([&]{pica_texture_layout(image);},"Unsupported pixel format rejected");
}
void projection_and_draws() {
    std::vector<std::uint8_t> lower(bottom_width*screen_height*3);
    const ImageView dashboard{lower,bottom_width,screen_height,bottom_width*3};
    std::array<PicaVertex,6> vertices{};
    for(auto& vertex:vertices) vertex.position={16,24,512};
    std::array<PicaDraw,2> draws{{{0,3},{3,3}}};
    PicaFrame frame{plan_frame(1,true,ScreenUse::world),vertices,draws,{}};
    validate_pica_frame(frame,dashboard);require(frame.plan.eye_count==2,"One geometry stream, two projections");
    for(unsigned eye=0;eye<2;++eye) {
        const auto matrix=pica_multiply(PicaProjection(frame.plan,eye).rows(),pica_identity);
        const auto clip=PicaProjection(frame.plan,eye).clip_position(vertices[0].position);
        require(clip.has_value(),"GPU projection accepts finite geometry");
        const auto expected=project(frame.plan,eye,16,24,512);
        require(expected.has_value(),"Independent off-axis projection");
        require(std::abs(top_width*.5*(1-(*clip)[1]/(*clip)[3])-(*expected)[0])<.0001,"GPU eye horizontal projection");
        require(std::abs(screen_height*.5*(1-(*clip)[0]/(*clip)[3])-(*expected)[1])<.0001,"GPU LCD vertical projection");
        require(matrix==PicaProjection(frame.plan,eye).rows(),"Identity model cannot alter projection");
    }
    const auto screen=pica_screen_matrix(top_width);
    require(screen[0][1]==-2.F/screen_height && screen[1][0]==-2.F/top_width
        && screen[2][3]==-.5F && screen[3][3]==1,"Mono overlays use rotated LCD and PICA depth");
    draws[1].space=PicaSpace::screen;draws[1].depth_test=false;draws[1].depth_write=false;
    validate_pica_frame(frame,dashboard);require(draws[1].first==3,"Authored screen/world pass order retained");
    require(pica_draw_matrix(frame.plan,0,draws[1])==pica_draw_matrix(frame.plan,1,draws[1]),"HUD is at screen depth, not sky depth");
    draws[1].space=PicaSpace::scenery;validate_pica_frame(frame,dashboard);
    for(unsigned eye=0;eye<2;++eye) {
        const auto scenery=pica_draw_matrix(frame.plan,eye,draws[1]);
        const double displayed_x=top_width*.5*(1-scenery[1][3]);
        require(std::abs(displayed_x-background_offset(frame.plan,eye))<.0001,"Background projects at infinity independently for each eye");
    }
    draws[1].space=PicaSpace::screen;
    const auto valid=[&]{validate_pica_frame(frame,dashboard);};
    draws[1].depth_write=true;rejects(valid,"Screen overlay cannot write world depth");draws[1].depth_write=false;
    draws[1].first=2;rejects(valid,"Overlapping geometry ranges rejected");draws[1].first=4;rejects(valid,"Missing primitive rejected");draws[1].first=3;
    draws[1].count=2;rejects(valid,"Partial triangle rejected");draws[1].count=6;rejects(valid,"Out-of-range draw rejected");draws[1].count=3;
    draws[1].texture=0;rejects(valid,"Unknown texture rejected");draws[1].texture=pica_no_texture;
    draws[0].model[0][0]=std::numeric_limits<float>::quiet_NaN();rejects(valid,"Non-finite model rejected");draws[0].model=pica_identity;
    draws[0].model[3][0]=1;rejects(valid,"Non-affine model rejected");draws[0].model=pica_identity;
    vertices[0].position[2]=std::numeric_limits<float>::infinity();rejects(valid,"Invalid position rejected");vertices[0].position[2]=512;
    vertices[0].colour[0]=1.1F;rejects(valid,"Invalid vertex colour rejected");vertices[0].colour[0]=1;
    frame.plan.eye_count=0;rejects(valid,"Missing eye rejected");frame.plan.eye_count=3;rejects(valid,"Third eye rejected");frame.plan.eye_count=2;
    frame.plan=plan_frame(0,true,ScreenUse::world);valid();require(frame.plan.eye_count==1,"Slider zero has no second eye work");
    frame.plan=plan_frame(1,true,ScreenUse::setup);valid();require(!frame.plan.stereo,"Setup labels remain mono");
    auto invalid_dashboard=dashboard;invalid_dashboard.pixels=std::span(lower).first(lower.size()-1);
    rejects([&]{validate_pica_frame(frame,invalid_dashboard);},"Incomplete dashboard rejected before GPU submission");
    frame.draws=std::span(draws).first(1);rejects(valid,"Unclaimed tail geometry rejected");frame.draws=draws;
    std::vector<PicaVertex> too_many(pica_vertex_limit+1);frame.vertices=too_many;rejects(valid,"Geometry budget is enforced without dropping faces");frame.vertices=vertices;
    std::vector<PicaDraw> too_many_draws(pica_draw_limit+1);frame.draws=too_many_draws;rejects(valid,"Draw budget is enforced");frame.draws=draws;
    std::vector<std::uint8_t> pixels(512*512*4);
    const PicaImage image{pixels,512,512,512*4,4};
    std::array<PicaImage,4> images{image,image,image,image};
    frame.textures=std::span(images).first(3);valid();
    frame.textures=images;rejects(valid,"Texture budget includes the padded lower-screen upload");frame.textures={};
    frame.vertices={};frame.draws={};valid();require(true,"Clear/dashboard-only native frames are allowed");
}
void layer_upload() {
    constexpr unsigned spread[]{0,1,4,5,16,17,20,21};
    for(unsigned width:{1U,7U,8U,9U,31U}) for(unsigned height:{1U,7U,17U}) {
        const unsigned pitch=width*4+3,layer_pitch=width+5;
        std::vector<std::uint8_t> pixels(pitch*height,0xAA),layers(layer_pitch*height,0xBB);
        constexpr unsigned classes[]{0,1,2,4,8,16,32};unsigned mask=0;
        for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
            const auto layer=classes[(x+y*3)%7];layers[y*layer_pitch+x]=std::uint8_t(layer);mask|=layer;
            pixels[y*pitch+x*4+3]=layer?255:0;
        }
        const PicaImage image{pixels,width,height,pitch,4,false,layers,layer_pitch};
        const auto layout=pica_texture_layout(image);
        require(pica_resident_texture_bytes(image)==layout.width*layout.height*5,"PPU budget must include one-byte A8 provenance, not six RGBA copies");
        std::vector<std::uint8_t> packed(layout.width*layout.height+2,0xCC);
        require(pack_pica_layers(image,std::span(packed).subspan(1,packed.size()-2))==mask,"Source layer population mask incorrect");
        require(packed.front()==0xCC && packed.back()==0xCC,"Layer upload crossed allocated storage");
        for(unsigned y=0;y<layout.height;++y) for(unsigned x=0;x<layout.width;++x) {
            const auto ty=layout.height-1-y;
            const auto offset=1+((ty/8)*(layout.width/8)+x/8)*64+spread[x%8]+2*spread[ty%8];
            require(packed[offset]==layers[std::min(y,height-1)*layer_pitch+std::min(x,width-1)],"A8 Morton/vertical flip/row stride/edge padding differs from colour texture");
        }
        if(std::has_single_bit(width) && std::has_single_bit(height)) {
            auto repeated=image;repeated.repeat=true;
            pack_pica_layers(repeated,std::span(packed).subspan(1,packed.size()-2));
            for(unsigned y=0;y<layout.height;++y) for(unsigned x=0;x<layout.width;++x) {
                const auto ty=layout.height-1-y;
                const auto offset=1+((ty/8)*(layout.width/8)+x/8)*64+spread[x%8]+2*spread[ty%8];
                require(packed[offset]==layers[(y%height)*layer_pitch+(x%width)],"Repeated A8 guards disagree with repeated RGBA artwork");
            }
        }
        auto bad=image;bad.layer_pitch=width-1;
        rejects([&]{pica_texture_layout(bad);},"Short layer pitch accepted");
        bad=image;bad.source_layers=std::span(layers).first(layer_pitch*(height-1)+width-1);
        rejects([&]{pica_texture_layout(bad);},"Truncated source ownership accepted");
        layers[0]=3;pixels[3]=255;
        rejects([&]{pack_pica_layers(image,std::span(packed).subspan(1,packed.size()-2));},"Ambiguous multi-layer pixel accepted");
        layers[0]=1;pixels[3]=0;
        rejects([&]{validate_pica_layers(image);},"Invisible pixel may not overwrite stencil ownership");
        layers[0]=0;pixels[3]=255;
        rejects([&]{validate_pica_layers(image);},"Opaque unclassified PPU pixel accepted");
        layers[0]=1;pixels[3]=128;
        rejects([&]{validate_pica_layers(image);},"Source layer ID cannot replace translucent alpha");
    }
    std::vector<std::uint8_t> rgba(8*8*4),layers(8*8);PicaImage image{rgba,8,8,32,4,false,layers,8};
    rejects([&]{pack_pica_layers(image,layers);},"A8 upload may not overwrite source provenance");
    rejects([&]{pack_pica_layers(image,std::span(rgba).first(64));},"A8 upload may not overwrite RGBA input");
    Canvas dashboard;const auto plan=plan_frame(1,true,ScreenUse::world);
    std::array<PicaVertex,3> vertices{{{{0,0,0}},{{400,0,0}},{{0,240,0}}}};
    PicaDraw draw{0,3,0,pica_identity,PicaSpace::screen,false,false,true};
    PicaFrame frame{plan,vertices,std::span(&draw,1),std::span(&image,1)};
    validate_pica_frame(frame,dashboard.view());
    draw.space=PicaSpace::world;
    rejects([&]{validate_pica_frame(frame,dashboard.view());},"Per-pixel PPU stencil mask accepted on perspective world geometry");
    draw.space=PicaSpace::screen;vertices[0].colour[3]=.5F;
    rejects([&]{validate_pica_frame(frame,dashboard.view());},"Per-pixel PPU stencil masks cannot silently discard vertex opacity");
    vertices[0].colour[3]=1;draw.source_layer=3;
    rejects([&]{validate_pica_frame(frame,dashboard.view());},"Ambiguous native draw layer accepted");
    std::vector<std::uint8_t> large_rgba(512*512*4),large_layers(512*512);
    const PicaImage large{large_rgba,512,512,512*4,4,false,large_layers,512};
    const std::array<PicaImage,3> images{large,large,large};
    frame.vertices={};frame.draws={};frame.textures=std::span(images).first(2);
    validate_pica_frame(frame,dashboard.view());
    frame.textures=images;
    rejects([&]{validate_pica_frame(frame,dashboard.view());},"Combined 3DS resident budget omitted A8 provenance bytes");
}
}
int main() try {
    texture_upload();projection_and_draws();layer_upload();
    std::cout<<"3DS PICA upload/projection/pass contracts: "<<checks<<" checks passed\n";
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}

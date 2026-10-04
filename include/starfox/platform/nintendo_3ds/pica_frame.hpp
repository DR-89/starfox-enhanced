#pragma once
#include "starfox/platform/nintendo_3ds/pica_projection.hpp"
#include <bit>

namespace starfox::platform::nintendo_3ds {
inline constexpr unsigned pica_vertex_limit=32'766,pica_draw_limit=256;
// There can be one distinct source texture per submitted draw (EX's texture
// test models exceed 32). This is a metadata bound, not a PICA sampler limit:
// only the current draw's texture is bound. The 4 MiB resident-byte limit still
// includes all padded textures and the lower LCD, independently of this count.
inline constexpr unsigned pica_texture_limit=pica_draw_limit;
inline constexpr unsigned pica_texture_budget=4*1024*1024;
inline constexpr unsigned pica_no_texture=~0U;
using PicaMatrix=PicaProjection::Rows;
inline constexpr PicaMatrix pica_identity{{{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}}};
struct PicaVertex {
    Point3 position; // Model-local; GPU matrix produces X-right/Y-up/Z-forward.
    std::array<float,4> colour{1,1,1,1};
    std::array<float,2> uv{}; // Logical texture, top-left origin, before padding.
    bool operator==(const PicaVertex&) const=default;
};
static_assert(sizeof(PicaVertex)==9*sizeof(float));
enum class PicaSpace {world,screen,scenery}; // Scenery is at infinity, not HUD depth.
struct PicaDraw {
    unsigned first{},count{},texture{pica_no_texture};
    PicaMatrix model{pica_identity};
    PicaSpace space{PicaSpace::world};
    bool depth_test{true},depth_write{true},alpha_blend{};
    // Two source inks remain distinct at fixed LCD pixel parity. The shader
    // emits homogeneous screen UV/Q, sampled as a projection texture; ordinary
    // model UVs must not stretch the checkerboard along perspective geometry.
    bool screen_dither{};
    std::array<std::uint8_t,4> dither_odd{}; // TEV constant; one shared parity mask.
};
struct PicaImage {
    std::span<const std::uint8_t> pixels;
    unsigned width{},height{},pitch{},channels{4}; // RGB24 or RGBA8, row-major.
    bool repeat{}; // Source wrapping artwork must have power-of-two dimensions.
};
struct PicaTextureLayout {
    unsigned width{},height{},bytes{};
    std::array<float,2> uv_scale{};
};
inline PicaTextureLayout pica_texture_layout(PicaImage image) {
    if(!image.width || image.width>1024 || !image.height || image.height>1024
        || (image.channels!=3 && image.channels!=4) || image.pitch<image.width*image.channels
        || image.pitch>16384 || !image.pixels.data()
        || image.pixels.size()<std::size_t(image.pitch)*(image.height-1)+image.width*image.channels
        || (image.repeat && (!std::has_single_bit(image.width) || !std::has_single_bit(image.height))))
        throw std::invalid_argument("Invalid 3DS GPU texture");
    const auto w=std::max(8U,std::bit_ceil(image.width)),h=std::max(8U,std::bit_ceil(image.height));
    return {w,h,w*h*4,{float(image.width)/w,float(image.height)/h}};
}
inline unsigned pica_texel_offset(unsigned x,unsigned y,unsigned width) noexcept {
    // PICA RGBA8 uses 8x8 Morton tiles, not a linear RGBA framebuffer.
    unsigned morton{};
    for(unsigned bit=0;bit<3;++bit) {
        morton|=((x>>bit)&1U)<<(bit*2);
        morton|=((y>>bit)&1U)<<(bit*2+1);
    }
    return (((y/8)*(width/8)+x/8)*64+morton)*4;
}
inline void pack_pica_texture(PicaImage source,std::span<std::uint8_t> destination) {
    const auto layout=pica_texture_layout(source);
    if(destination.size()!=layout.bytes || !destination.data())
        throw std::invalid_argument("Incomplete 3DS GPU texture allocation");
    const auto src=reinterpret_cast<std::uintptr_t>(source.pixels.data()),dst=reinterpret_cast<std::uintptr_t>(destination.data());
    if((dst>=src && dst-src<source.pixels.size()) || (src>=dst && src-dst<destination.size()))
        throw std::invalid_argument("3DS texture upload requires independent storage");
    for(unsigned y=0;y<layout.height;++y) for(unsigned x=0;x<layout.width;++x) {
        const auto sx=source.repeat?x%source.width:std::min(x,source.width-1);
        const auto sy=source.repeat?y%source.height:std::min(y,source.height-1);
        const auto from=std::size_t(sy)*source.pitch+sx*source.channels;
        const auto to=pica_texel_offset(x,layout.height-1-y,layout.width);
        destination[to]=source.channels==4?source.pixels[from+3]:255; // ABGR bytes.
        destination[to+1]=source.pixels[from+2];destination[to+2]=source.pixels[from+1];destination[to+3]=source.pixels[from];
    }
}
inline PicaMatrix pica_screen_matrix(unsigned width,unsigned height=screen_height) {
    if((width!=top_width && width!=bottom_width) || height!=screen_height)
        throw std::invalid_argument("Invalid 3DS screen projection");
    // Top-left pixel coordinates; clockwise LCD rotation and [-w,0] depth.
    return {{{0,-2.F/height,0,1},{-2.F/width,0,0,1},{0,0,0,-.5F},{0,0,0,1}}};
}
inline PicaMatrix pica_multiply(const PicaMatrix& a,const PicaMatrix& b) {
    PicaMatrix result{};
    for(unsigned row=0;row<4;++row) for(unsigned col=0;col<4;++col) {
        double value=0;
        for(unsigned k=0;k<4;++k) value+=double(a[row][k])*b[k][col];
        if(!std::isfinite(value) || std::abs(value)>std::numeric_limits<float>::max())
            throw std::invalid_argument("Unrepresentable 3DS GPU matrix");
        result[row][col]=float(value);
    }
    return result;
}
inline PicaMatrix pica_draw_matrix(const FramePlan& plan,unsigned eye,const PicaDraw& draw) {
    if(eye>=plan.eye_count) throw std::invalid_argument("Inactive 3DS GPU draw eye");
    if(draw.space==PicaSpace::world) return pica_multiply(PicaProjection(plan,eye).rows(),draw.model);
    auto model=draw.model;
    if(draw.space==PicaSpace::scenery) model[0][3]+=background_offset(plan,eye);
    else if(draw.space!=PicaSpace::screen) throw std::invalid_argument("Unknown 3DS GPU coordinate space");
    return pica_multiply(pica_screen_matrix(top_width),model);
}
struct PicaFrame {
    FramePlan plan;
    std::span<const PicaVertex> vertices; // One immutable geometry source for both eyes.
    std::span<const PicaDraw> draws; // Authored painter/pass order, never sorted by texture.
    std::span<const PicaImage> textures;
    Rgb clear{8,15,28};
};
// Complete validation precedes any frame recording/texture replacement. This
// is not a primitive converter: unresolved source faces must not be omitted.
inline void validate_pica_frame(const PicaFrame& frame,ImageView dashboard) {
    if(!valid_image(dashboard,bottom_width,screen_height) || !dashboard.pixels.data()
        || frame.vertices.size()>pica_vertex_limit || frame.draws.size()>pica_draw_limit
        || frame.textures.size()>pica_texture_limit)
        throw std::invalid_argument("Invalid 3DS GPU frame dimensions/budget");
    std::array<PicaProjection::Rows,2> projections{};
    for(unsigned eye=0;eye<frame.plan.eye_count;++eye) {
        if(eye>=2) throw std::invalid_argument("Invalid 3DS GPU eye count");
        projections[eye]=PicaProjection(frame.plan,eye).rows();
    }
    if(frame.plan.eye_count!=(frame.plan.stereo?2U:1U))
        throw std::invalid_argument("Incomplete 3DS eye plan");
    unsigned bytes=pica_texture_layout({dashboard.pixels,dashboard.width,dashboard.height,dashboard.pitch,3}).bytes;
    for(auto texture:frame.textures) {
        const auto size=pica_texture_layout(texture).bytes;
        if(size>pica_texture_budget-bytes) throw std::invalid_argument("3DS GPU texture budget exceeded");
        bytes+=size;
    }
    unsigned cursor=0;
    for(const auto& draw:frame.draws) {
        if(draw.first!=cursor || !draw.count || draw.count%3
            || draw.count>frame.vertices.size()-cursor || (draw.texture!=pica_no_texture && draw.texture>=frame.textures.size())
            || (draw.space!=PicaSpace::world && draw.space!=PicaSpace::screen && draw.space!=PicaSpace::scenery)
            || draw.model[3]!=std::array<float,4>{0,0,0,1}
            || (draw.space!=PicaSpace::world && (draw.depth_test || draw.depth_write))
            || (draw.depth_write && !draw.depth_test))
            throw std::invalid_argument("Invalid/omitted 3DS GPU draw range");
        if(draw.screen_dither && (draw.texture==pica_no_texture
            || frame.textures[draw.texture].width!=8 || frame.textures[draw.texture].height!=8
            || !frame.textures[draw.texture].repeat))
            throw std::invalid_argument("Invalid 3DS source dither texture");
        for(const auto& row:draw.model) for(float value:row) if(!std::isfinite(value))
            throw std::invalid_argument("Non-finite 3DS model matrix");
        for(unsigned eye=0;eye<frame.plan.eye_count;++eye)
            static_cast<void>(pica_draw_matrix(frame.plan,eye,draw));
        for(unsigned i=cursor;i<cursor+draw.count;++i) {
            const auto& vertex=frame.vertices[i];
            for(float value:vertex.position) if(!std::isfinite(value)) throw std::invalid_argument("Non-finite 3DS vertex");
            for(float value:vertex.colour) if(!std::isfinite(value) || value<0 || value>1) throw std::invalid_argument("Invalid 3DS vertex colour");
            for(float value:vertex.uv) if(!std::isfinite(value) || std::abs(value)>65536
                || (draw.texture!=pica_no_texture && !frame.textures[draw.texture].repeat && (value<0 || value>1)))
                throw std::invalid_argument("Invalid 3DS texture coordinate");
        }
        cursor+=draw.count;
    }
    if(cursor!=frame.vertices.size()) throw std::invalid_argument("3DS GPU frame has unsubmitted vertices");
}
} // namespace starfox::platform::nintendo_3ds

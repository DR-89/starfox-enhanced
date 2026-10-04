#include "native_gpu.hpp"
#include <atomic>
#include <cstring>
extern "C" {
#include <3ds/types.h>
#include <3ds/result.h>
#include <3ds/allocator/linear.h>
#include <3ds/gfx.h>
#include <3ds/gpu/gpu.h>
#include <3ds/gpu/shaderProgram.h>
#include <3ds/gpu/gx.h>
}
#include <citro3d.h>

namespace starfox::platform::nintendo_3ds {
namespace {
constexpr unsigned command_bytes=1024*1024;
constexpr u32 transfer_flags=GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)
    |GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8)
    |GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
std::atomic_flag gpu_owned=ATOMIC_FLAG_INIT;
struct GpuLease {
    GpuLease() {if(gpu_owned.test_and_set()) throw std::logic_error("3DS GPU presenter already owned");}
    ~GpuLease() {gpu_owned.clear();}
};
struct FrameEnd {
    // All changed buffers/textures are explicitly flushed. Do not flush the
    // entire linear heap (including unrelated PCM/unchanged art) every frame.
    ~FrameEnd() {C3D_FrameEnd(GX_CMDLIST_FLUSH);}
};
u32 clear_colour(Rgb colour) {return (u32(colour.r)<<24)|(u32(colour.g)<<16)|(u32(colour.b)<<8)|255;}
void upload_matrix(int location,const PicaMatrix& rows) {
    C3D_Mtx matrix{};
    for(unsigned i=0;i<4;++i) {
        matrix.r[i].x=rows[i][0];matrix.r[i].y=rows[i][1];
        matrix.r[i].z=rows[i][2];matrix.r[i].w=rows[i][3];
    }
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,location,&matrix);
}
struct ResidentTexture {
    C3D_Tex texture{};
    std::vector<std::uint8_t> pixels;
    unsigned width{},height{},channels{};
    bool ready{},repeat{};
    void release() noexcept {
        if(ready) C3D_TexDelete(&texture);
        texture={};ready=false;pixels.clear();width=height=channels=0;
    }
    bool matches(PicaImage image) const noexcept {
        if(!ready || image.width!=width || image.height!=height || image.channels!=channels || image.repeat!=repeat) return false;
        const auto row_bytes=std::size_t(width)*channels;
        for(unsigned y=0;y<height;++y)
            if(!std::equal(image.pixels.begin()+std::size_t(y)*image.pitch,
                image.pixels.begin()+std::size_t(y)*image.pitch+row_bytes,pixels.begin()+y*row_bytes)) return false;
        return true;
    }
    void update(PicaImage image) {
        if(matches(image)) return;
        const auto layout=pica_texture_layout(image);
        std::vector<std::uint8_t> next(std::size_t(image.width)*image.height*image.channels);
        for(unsigned y=0;y<image.height;++y)
            std::copy_n(image.pixels.begin()+std::size_t(y)*image.pitch,std::size_t(image.width)*image.channels,
                next.begin()+std::size_t(y)*image.width*image.channels);
        if(!ready || texture.width!=layout.width || texture.height!=layout.height) {
            release();
            if(!C3D_TexInit(&texture,layout.width,layout.height,GPU_RGBA8))
                throw std::runtime_error("3DS GPU texture allocation failed");
            ready=true;
        }
        pack_pica_texture(image,{static_cast<std::uint8_t*>(texture.data),layout.bytes});
        C3D_TexSetFilter(&texture,GPU_NEAREST,GPU_NEAREST);
        C3D_TexSetWrap(&texture,image.repeat?GPU_REPEAT:GPU_CLAMP_TO_EDGE,image.repeat?GPU_REPEAT:GPU_CLAMP_TO_EDGE);
        C3D_TexFlush(&texture);
        width=image.width;height=image.height;channels=image.channels;repeat=image.repeat;pixels=std::move(next);
    }
};
} // namespace
struct NativeGpu::Impl {
    GpuLease lease;
    bool initialized{},program_ready{};
    PicaVertex* vbo{};
    std::vector<PicaVertex> cached_vertices;
    std::vector<u32> shader_words;
    DVLB_s* library{};
    shaderProgram_s program{};
    int transform_location{-1},uv_location{-1};
    std::array<C3D_RenderTarget*,2> top{};
    C3D_RenderTarget* bottom{};
    std::array<ResidentTexture,pica_texture_limit> textures;
    ResidentTexture dashboard;
    Impl(std::span<const std::uint8_t> shader) {
        if(shader.size()<32 || shader.size()>65536 || shader.size()%4 || !shader.data()
            || std::memcmp(shader.data(),"DVLB",4)) throw std::invalid_argument("Invalid 3DS GPU shader library");
        if(!C3D_Init(command_bytes)) throw std::runtime_error("Citro3D initialization failed");
        initialized=true;
        try {
            shader_words.resize(shader.size()/4);std::memcpy(shader_words.data(),shader.data(),shader.size());
            library=DVLB_ParseFile(shader_words.data(),shader.size());
            if(!library || library->numDVLE!=1) throw std::runtime_error("3DS GPU vertex shader parse failed");
            if(R_FAILED(shaderProgramInit(&program))) throw std::runtime_error("3DS GPU program allocation failed");
            program_ready=true;
            if(R_FAILED(shaderProgramSetVsh(&program,&library->DVLE[0]))) throw std::runtime_error("3DS GPU vertex shader binding failed");
            transform_location=shaderInstanceGetUniformLocation(program.vertexShader,"transform");
            uv_location=shaderInstanceGetUniformLocation(program.vertexShader,"uv_scale");
            if(transform_location<0 || uv_location<0) throw std::runtime_error("3DS GPU shader uniforms missing");
            top[0]=make_target(top_width,GFX_TOP,GFX_LEFT,true);
            bottom=make_target(bottom_width,GFX_BOTTOM,GFX_LEFT,false);
            vbo=static_cast<PicaVertex*>(linearAlloc((pica_vertex_limit+6)*sizeof(PicaVertex)));
            if(!vbo) throw std::runtime_error("3DS GPU vertex-buffer allocation failed");
            const std::array<Point3,4> corners{{{0,0,0},{float(bottom_width),0,0},
                {float(bottom_width),float(screen_height),0},{0,float(screen_height),0}}};
            const std::array<std::array<float,2>,4> uv{{{0,0},{1,0},{1,1},{0,1}}};
            unsigned index=pica_vertex_limit;
            for(auto corner:{0U,1U,2U,0U,2U,3U}) vbo[index++]={corners[corner],{1,1,1,1},uv[corner]};
            if(R_FAILED(GSPGPU_FlushDataCache(vbo+pica_vertex_limit,6*sizeof(PicaVertex))))
                throw std::runtime_error("3DS GPU HUD vertex flush failed");
        } catch(...) {shutdown();throw;}
    }
    ~Impl() {shutdown();}
    static C3D_RenderTarget* make_target(unsigned width,gfxScreen_t screen,gfx3dSide_t side,bool depth) {
        auto* target=C3D_RenderTargetCreate(screen_height,width,GPU_RB_RGBA8,
            depth?C3D_DEPTHTYPE(GPU_RB_DEPTH24_STENCIL8):C3D_DEPTHTYPE(-1));
        if(!target) throw std::runtime_error("3DS GPU LCD/depth target allocation failed");
        C3D_RenderTargetSetOutput(target,screen,side,transfer_flags);return target;
    }
    void shutdown() noexcept {
        // Fini waits for command/transfer completion and owns target cleanup.
        // Shader data, textures and the shared VBO stay alive until it returns.
        if(initialized) {C3D_Fini();initialized=false;}
        for(auto& texture:textures) texture.release();
        dashboard.release();
        if(vbo) {linearFree(vbo);vbo=nullptr;}
        if(program_ready) {shaderProgramFree(&program);program_ready=false;}
        if(library) {DVLB_Free(library);library=nullptr;}
    }
    void configure() {
        C3D_BindProgram(&program);
        auto* attrs=C3D_GetAttrInfo();AttrInfo_Init(attrs);
        AttrInfo_AddLoader(attrs,0,GPU_FLOAT,3);AttrInfo_AddLoader(attrs,1,GPU_FLOAT,4);AttrInfo_AddLoader(attrs,2,GPU_FLOAT,2);
        auto* buffers=C3D_GetBufInfo();BufInfo_Init(buffers);
        if(BufInfo_Add(buffers,vbo,sizeof(PicaVertex),3,0x210)<0) throw std::runtime_error("3DS GPU vertex layout rejected");
        C3D_CullFace(GPU_CULL_NONE); // Source visibility is not generic winding.
        C3D_DepthMap(true,-1,0);C3D_AlphaTest(true,GPU_GREATER,0);
        C3D_EarlyDepthTest(false,GPU_EARLYDEPTH_GEQUAL,0);
        for(unsigned stage=0;stage<6;++stage) C3D_TexEnvInit(C3D_GetTexEnv(stage));
    }
    void material(ResidentTexture* texture,bool alpha,bool depth,bool write) {
        auto* env=C3D_GetTexEnv(0);C3D_TexEnvInit(env);
        if(texture) {
            C3D_TexBind(0,&texture->texture);
            C3D_TexEnvSrc(env,C3D_Both,GPU_PRIMARY_COLOR,GPU_TEXTURE0);
            C3D_TexEnvFunc(env,C3D_Both,GPU_MODULATE);
            C3D_FVUnifSet(GPU_VERTEX_SHADER,uv_location,float(texture->width)/texture->texture.width,
                float(texture->height)/texture->texture.height,0,0);
        } else {
            C3D_TexBind(0,nullptr);C3D_TexEnvSrc(env,C3D_Both,GPU_PRIMARY_COLOR);
            C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);C3D_FVUnifSet(GPU_VERTEX_SHADER,uv_location,1,1,0,0);
        }
        C3D_DepthTest(depth,GPU_GEQUAL,write?GPU_WRITE_ALL:GPU_WRITE_COLOR);
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,alpha?GPU_SRC_ALPHA:GPU_ONE,
            alpha?GPU_ONE_MINUS_SRC_ALPHA:GPU_ZERO,GPU_ONE,alpha?GPU_ONE_MINUS_SRC_ALPHA:GPU_ZERO);
    }
};
NativeGpu::NativeGpu(std::span<const std::uint8_t> shader):impl_(std::make_unique<Impl>(shader)) {}
NativeGpu::~NativeGpu()=default;
void NativeGpu::present(const PicaFrame& frame,ImageView lower) {
    validate_pica_frame(frame,lower);
    if(!C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) throw std::runtime_error("3DS GPU frame unavailable");
    FrameEnd end; // Includes failure exits; uploads complete before targets are marked used.
    for(unsigned i=frame.textures.size();i<impl_->textures.size();++i) impl_->textures[i].release();
    for(unsigned i=0;i<frame.textures.size();++i) impl_->textures[i].update(frame.textures[i]);
    impl_->dashboard.update({lower.pixels,lower.width,lower.height,lower.pitch,3});
    if(impl_->cached_vertices.size()!=frame.vertices.size()
        || !std::equal(frame.vertices.begin(),frame.vertices.end(),impl_->cached_vertices.begin())) {
        std::vector<PicaVertex> next(frame.vertices.begin(),frame.vertices.end());
        if(!next.empty()) {
            std::memcpy(impl_->vbo,next.data(),next.size()*sizeof(PicaVertex));
            if(R_FAILED(GSPGPU_FlushDataCache(impl_->vbo,next.size()*sizeof(PicaVertex))))
                throw std::runtime_error("3DS GPU geometry flush failed");
        }
        impl_->cached_vertices=std::move(next);
    }
    if(frame.plan.stereo && !impl_->top[1]) impl_->top[1]=Impl::make_target(top_width,GFX_TOP,GFX_RIGHT,true);
    impl_->configure();gfxSet3D(frame.plan.stereo);
    for(unsigned eye=0;eye<frame.plan.eye_count;++eye) {
        auto* target=impl_->top[eye];C3D_RenderTargetClear(target,C3D_CLEAR_ALL,clear_colour(frame.clear),0);
        if(!C3D_FrameDrawOn(target)) throw std::runtime_error("3DS GPU eye target unavailable");
        for(const auto& draw:frame.draws) {
            upload_matrix(impl_->transform_location,pica_draw_matrix(frame.plan,eye,draw));
            impl_->material(draw.texture==pica_no_texture?nullptr:&impl_->textures[draw.texture],
                draw.alpha_blend,draw.depth_test,draw.depth_write);
            C3D_DrawArrays(GPU_TRIANGLES,draw.first,draw.count);
        }
    }
    C3D_RenderTargetClear(impl_->bottom,C3D_CLEAR_COLOR,0,0);
    if(!C3D_FrameDrawOn(impl_->bottom)) throw std::runtime_error("3DS GPU HUD target unavailable");
    upload_matrix(impl_->transform_location,pica_screen_matrix(bottom_width));
    impl_->material(&impl_->dashboard,false,false,false);
    C3D_DrawArrays(GPU_TRIANGLES,pica_vertex_limit,6);
}
} // namespace starfox::platform::nintendo_3ds

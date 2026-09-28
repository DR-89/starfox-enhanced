#include "starfox/render/gpu_volumetric_fog.hpp"
#include "starfox/render/sdl_gpu_effects.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <iostream>
#include <stdexcept>
using namespace starfox::render;
void require(bool ok,const std::string& message) {if(!ok) throw std::runtime_error(message);}
int main(int argc,char** argv) {
    SDL_GPUDevice* device=nullptr;
    try {
        require(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
        device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV|SDL_GPU_SHADERFORMAT_DXIL|SDL_GPU_SHADERFORMAT_MSL,
            true,argc>1?argv[1]:nullptr);
        require(device,SDL_GetError());
        GpuVolumetricFog gpu;
        shadows::Scene empty;empty.build();
        shadows::Scene scene;
        scene.add({{-2,-2,5},{2,-2,5},{0,2,5}});
        scene.add({{-100,-5,-100},{100,-5,-100},{100,-5,50}});
        scene.add({{-100,-5,-100},{100,-5,50},{-100,-5,50}});scene.build();
        VolumetricMedium medium;medium.extinction=.01;medium.maximum_distance=100;medium.anisotropy=.35;
        const shadows::Vec3 light{0,-1,0};
        float worst=0;unsigned cases=0;
        for(bool background_only:{false,true}) for(double eye:{-1.,0.,1.}) for(bool geometry:{false,true}) for(bool ground:{false,true}) for(unsigned samples:{1U,16U,64U,128U}) {
            medium.samples=samples;
            shadows::Scene current;
            const shadows::Vec3 eye_offset{-eye,0,0};
            for(const auto& t:(geometry?scene:empty).triangles()) current.add({t.a+eye_offset,t.b+eye_offset,t.c+eye_offset});
            current.build();
            const unsigned width=9,height=7;
            const VolumetricProjection projection{4,5,4.5+4*eye/20,3.5};
            const auto plane=ground?std::optional<VolumetricGround>{{{-eye,0,10},{0,1,-1}}}:std::nullopt;
            require(gpu.render(device,current,projection,width,height,medium,light,plane,background_only),gpu.status());
            const auto output=gpu.output();require(output.buffer&&output.width==width&&output.height==height,"Missing resident fog output");
            std::vector<std::array<float,4>> actual;
            require(gpu.readback(actual),gpu.status());
            std::vector<std::uint8_t> coverage(width*height,1);
            std::vector<VolumetricPixel> guides;
            require(build_volumetric_guides(current,projection,width,height,coverage,medium.maximum_distance,plane,guides,background_only),"CPU guide failure");
            for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
                const auto i=y*width+x;
                const shadows::Vec3 ray{(x+.5-projection.center_x)/projection.focal_x,(y+.5-projection.center_y)/projection.focal_y,1};
                const auto distance=guides[i].view_depth==0?medium.maximum_distance:
                    guides[i].view_depth*std::sqrt(shadows::dot(ray,ray));
                const auto expected=integrate_volumetric_fog(medium,{},ray,distance,light,current);
                require(bool(expected),"CPU integral failure");
                const double values[]{expected->scattering.x,expected->scattering.y,expected->scattering.z,expected->transmittance};
                for(unsigned c=0;c<4;++c) {
                    const float delta=float(std::abs(actual[i][c]-values[c]));worst=std::max(worst,delta);
                    require(std::isfinite(actual[i][c])&&delta<.0001f,"GPU fog differs from CPU: case="+std::to_string(cases)
                        +" pixel="+std::to_string(i)+" channel="+std::to_string(c)+" delta="+std::to_string(delta)
                        +" expected="+std::to_string(values[c])+" actual="+std::to_string(actual[i][c])
                        +" depth="+std::to_string(guides[i].view_depth)+" T="+std::to_string(actual[i][3]));
                }
            }
            ++cases;
        }
        {
            Framebuffer frame(9,7);frame.enable_layer_tags(true);
            std::vector<std::uint8_t> source(9*7*4);
            for(unsigned i=0;i<63;++i) {
                source[i*4]=std::uint8_t(i*11);source[i*4+1]=std::uint8_t(i*7);source[i*4+2]=std::uint8_t(i*3);
                source[i*4+3]=i%7?200:0;
                if(i%5==0) frame.layer_tags()[i]=std::uint8_t(PixelLayer::two_d);
            }
            const VolumetricProjection projection{4,5,4.5,3.5};
            auto expected=source,actual=source;
            require(apply_volumetric_fog(medium,projection,frame,scene,light,std::nullopt,expected),"CPU composite failed");
            require(gpu.render(device,scene,projection,9,7,medium,light,std::nullopt),gpu.status());
            GpuEffectSettings settings;settings.volumetric=gpu.output();
            SdlGpuEffects compositor;
            require(compositor.apply(device,frame,actual,settings),compositor.status());
            for(unsigned i=0;i<actual.size();++i) {
                const bool protected_pixel=frame.layer_tags()[i/4]==std::uint8_t(PixelLayer::two_d)||source[(i/4)*4+3]==0;
                require(std::abs(int(actual[i])-int(expected[i]))<=(protected_pixel||i%4==3?0:1),
                    "GPU fog composite/HUD mismatch byte="+std::to_string(i));
            }
            actual=source;settings.volumetric={};
            require(compositor.apply(device,frame,actual,settings)&&actual==source,"Fog toggle retained stale composite binding");
            compositor.release_device();
        }
        medium.extinction=0;
        require(gpu.render(device,scene,{1,1,.5,.5},1,1,medium,light,std::nullopt),gpu.status());
        std::vector<std::array<float,4>> identity;
        require(gpu.readback(identity)&&identity.size()==1&&identity[0]==std::array<float,4>{0,0,0,1},"Zero density not identity");
        require(!gpu.render(device,scene,{0,1,.5,.5},1,1,medium,light,std::nullopt)&&!gpu.output().buffer,"Invalid input retained stale fog");
        require(!gpu.render(device,scene,{1,1,1e300,.5},1,1,medium,light,std::nullopt)&&!gpu.output().buffer,"Float overflow accepted");
        require(gpu.render(device,empty,{1,1,.5,.5},2,2,medium,light,std::nullopt),"Could not recover after invalid input");
        gpu.release_device();require(!gpu.output().buffer,"Release retained output");
        require(gpu.render(device,empty,{1,1,.5,.5},1,1,medium,light,std::nullopt),"Could not reacquire device");
        gpu.release_device();
        std::cout<<"GPU volumetric parity passed: "<<SDL_GetGPUDeviceDriver(device)<<", cases="<<cases<<", max error="<<worst<<'\n';
        SDL_DestroyGPUDevice(device);SDL_Quit();return 0;
    } catch(const std::exception& e) {
        std::cerr<<e.what()<<'\n';if(device) SDL_DestroyGPUDevice(device);SDL_Quit();return 1;
    }
}

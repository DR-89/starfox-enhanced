#pragma once
#include "starfox/render/volumetric_fog.hpp"
#include <memory>
#include <string>
namespace starfox::render {
// Borrowed float4 buffer: linear scattering RGB, transmittance A. Valid until
// the next dispatch/release. Release this owner before destroying its device.
struct GpuVolumetricOutput {void* device{};void* buffer{};unsigned width{},height{};};
class GpuVolumetricFog {
public:
    GpuVolumetricFog();
    ~GpuVolumetricFog();
    bool render(void* device,const shadows::Scene&,VolumetricProjection,unsigned width,unsigned height,
        const VolumetricMedium&,shadows::Vec3 light,std::optional<VolumetricGround>,bool background_only=false);
    GpuVolumetricOutput output() const;
    bool readback(std::vector<std::array<float,4>>&);
    void release_device() noexcept;
    const std::string& status() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

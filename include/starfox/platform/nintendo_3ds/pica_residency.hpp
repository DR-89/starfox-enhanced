#pragma once
#include "starfox/platform/nintendo_3ds/pica_frame.hpp"

namespace starfox::platform::nintendo_3ds {
struct PicaTextureRetention {
    bool colour{},layers{};
};
// Allocated (padded) dimensions, not logical image dimensions or row stride.
// Retain same-size storage for palette/scroll changes; discard obsolete A8
// ownership even when the colour allocation can be reused.
inline PicaTextureRetention pica_texture_retention(PicaImage image,
    unsigned colour_width,unsigned colour_height,unsigned layer_width,unsigned layer_height) {
    const auto layout=pica_texture_layout(image);
    return {colour_width==layout.width && colour_height==layout.height,
        !image.source_layers.empty() && layer_width==layout.width && layer_height==layout.height};
}
inline void release_pica_texture_cache(std::vector<std::uint8_t>& cache) noexcept {
    // clear() leaves each inactive slot's old peak allocation resident.
    std::vector<std::uint8_t>().swap(cache);
}
// The native presenter calls this ONLY after its previous GPU work completes.
// Preflight everything, release ALL obsolete allocations, THEN upload. Updating
// slots one at a time can exceed 4 MiB during a valid 4 MiB -> 4 MiB transition.
// The shared sequence is also exercised with an allocation-ledger test owner;
// that test is not a Citro3D/physical-device allocation or performance result.
template<class Resident,std::size_t Extent>
void update_pica_texture_residency(std::span<const PicaImage> images,PicaImage dashboard,
    std::span<Resident,Extent> textures,Resident& lower) {
    if(images.size()>textures.size() || textures.size()>pica_texture_limit)
        throw std::invalid_argument("Invalid 3DS resident texture slots");
    auto bytes=pica_resident_texture_bytes(dashboard);
    if(bytes>pica_texture_budget) throw std::invalid_argument("3DS resident dashboard budget exceeded");
    for(const auto image:images) {
        const auto size=pica_resident_texture_bytes(image);
        if(size>pica_texture_budget-bytes) throw std::invalid_argument("3DS resident texture budget exceeded");
        bytes+=size;
    }
    for(std::size_t i=images.size();i<textures.size();++i) textures[i].release();
    for(std::size_t i=0;i<images.size();++i) textures[i].prepare_layout(images[i]);
    lower.prepare_layout(dashboard);
    for(std::size_t i=0;i<images.size();++i) textures[i].update(images[i]);
    lower.update(dashboard);
}
} // namespace starfox::platform::nintendo_3ds

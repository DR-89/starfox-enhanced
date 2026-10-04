#pragma once
#include "starfox/platform/nintendo_3ds/game_presentation.hpp"
#include "starfox/platform/nintendo_3ds/pica_raster.hpp"

namespace starfox::platform::nintendo_3ds {
// Source-authored 2D painter groups around the native BG1 model stream.
// These are LCD artwork, not a finished-world stereo image. Outdoor terrain,
// panorama and source dust/grid have their own native geometry adapters.
struct GameLayerPlan {
    PpuBatch before_models,after_models;
    bool solid_frontend_margins{};
    bool operator==(const GameLayerPlan&) const=default;
};
[[nodiscard]] GameLayerPlan game_layer_plan(const GamePresentation&);
struct GameLayerFrames {PicaFrame before_models,after_models;Rgb clear;};
class GameLayers {
public:
    // Borrowed views remain valid until the next prepare on this owner.
    GameLayerFrames prepare(const GamePresentation&);
    [[nodiscard]] std::array<PpuRasterWork,2> work() const noexcept {return {before_.work(),after_.work()};}
private:
    PicaRaster before_,after_;
};
} // namespace starfox::platform::nintendo_3ds

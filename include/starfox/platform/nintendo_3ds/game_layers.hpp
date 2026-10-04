#pragma once
#include "starfox/platform/nintendo_3ds/game_presentation.hpp"
#include "starfox/platform/nintendo_3ds/pica_raster.hpp"
#include "starfox/platform/nintendo_3ds/game_scenery.hpp"
#include "starfox/platform/nintendo_3ds/pica_composite.hpp"

namespace starfox::platform::nintendo_3ds {
// Source-authored 2D painter groups around the native BG1 model stream.
// These are LCD artwork, not a finished-world stereo image. Outdoor terrain,
// panorama and source dust/grid have their own native geometry adapters.
struct GameLayerPlan {
    PpuBatch before_models,after_models;
    bool solid_frontend_margins{};
    // Only split at coordinate-space boundaries. Flattening this sequence or
    // collecting all BG2 passes first would change native sprite priorities.
    std::vector<PpuBatch> before_model_groups;
    std::vector<PpuBatch> after_model_groups;
    bool operator==(const GameLayerPlan&) const=default;
};
[[nodiscard]] GameLayerPlan game_layer_plan(const GamePresentation&);
[[nodiscard]] bool native_panorama_scene(const GamePresentation&) noexcept;
struct GameLayerFrames {PicaFrame before_models,after_models;Rgb clear;};
class GameLayers {
public:
    // Borrowed views remain valid until the next prepare on this owner.
    GameLayerFrames prepare(const GamePresentation&);
    [[nodiscard]] std::array<PpuRasterWork,2> work() const noexcept;
private:
    PicaRaster before_,after_;
    GameScenery scenery_;
    struct PainterOwner {
        PicaRaster raster;
        GameScenery receiver;
        std::vector<PicaImage> isolated_images;
    };
    using Owners=std::vector<std::unique_ptr<PainterOwner>>;
    PicaFrame prepare_groups(const GamePresentation&,std::span<const PpuBatch>,Owners&,
        std::vector<PicaFrame>&,PicaComposite&,PpuRasterWork&);
    Owners panorama_groups_,foreground_groups_;
    std::vector<PicaFrame> working_groups_,working_foreground_;
    PicaComposite panorama_,foreground_;
    PpuRasterWork retired_before_work_,retired_after_work_;
};
} // namespace starfox::platform::nintendo_3ds

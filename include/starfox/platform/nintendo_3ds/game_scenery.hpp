#pragma once
#include "starfox/platform/nintendo_3ds/game_presentation.hpp"
#include "starfox/platform/nintendo_3ds/pica_frame.hpp"

namespace starfox::platform::nintendo_3ds {
struct LandscapePlane {
    // LCD horizon y = centre + slope*(x-200). Height is source world units,
    // not a stereo setting or an arbitrary normalized-depth multiplier.
    double centre{},slope{},height{};
    bool operator==(const LandscapePlane&) const=default;
};
LandscapePlane source_landscape_plane(const GamePresentation&);
bool native_landscape_scene(const GamePresentation&) noexcept;

// Reuses the isolated BG2 raster, NOT a final scene image. Background artwork
// is at infinity; a finite camera plane provides terrain disparity/occlusion.
// One small immutable mesh and one borrowed texture serve both LCD eyes.
class GameScenery {
public:
    PicaFrame prepare(const GamePresentation&,const PicaFrame& bg2);
private:
    std::vector<PicaVertex> vertices_;
    std::array<PicaDraw,2> draws_{};
    std::array<PicaImage,1> image_{};
};
} // namespace starfox::platform::nintendo_3ds

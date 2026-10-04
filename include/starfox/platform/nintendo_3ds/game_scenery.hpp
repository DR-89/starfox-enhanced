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
bool native_water_scene(const GamePresentation&) noexcept;
double source_water_height(const GamePresentation&);
unsigned source_water_guard(const GamePresentation&);

// Reuses the isolated BG2 raster, NOT a final scene image. Background artwork
// is at infinity; a finite camera plane provides terrain disparity/occlusion.
// One immutable mesh and borrowed horizontal texture strips serve both eyes.
class GameScenery {
public:
    PicaFrame prepare(const GamePresentation&,const PicaFrame& bg2);
    PicaFrame prepare_water(const GamePresentation&,const PicaFrame& bg2,unsigned available_guard);
private:
    std::vector<PicaVertex> vertices_;
    std::vector<PicaDraw> draws_;
    std::vector<PicaImage> images_;
};
} // namespace starfox::platform::nintendo_3ds

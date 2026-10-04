#pragma once
#include "starfox/platform/nintendo_3ds/pica_frame.hpp"
#include "starfox/render/background_renderer.hpp"
#include "starfox/render/sprite_renderer.hpp"
#include <memory>

namespace starfox::platform::nintendo_3ds {
enum class PpuLayer {bg1,bg2,bg3,objects};
struct PpuPass {
    PpuLayer layer{PpuLayer::bg2};
    int priority{-1}; // -1 all; BG 0 low / 1 high; OBJ 0..3.
    std::optional<std::array<std::int16_t,2>> scroll{};
    bool extend_horizontal{true},wrap_horizontal{true},transparent_black{},mosaic_inset{};
    unsigned guard_inset{},single_occurrence_top_rows{};
    render::SpriteSelection sprites{render::SpriteSelection::all};
    bool operator==(const PpuPass&) const=default;
};
struct PpuBatch {
    // A batch is one contiguous source painter group. Never bake models or
    // combine passes that belong on opposite sides of a native geometry pass.
    std::vector<PpuPass> passes;
    PicaSpace space{PicaSpace::screen};
    bool expand_horizontal{};
    unsigned first_row{},last_row{224};
    bool operator==(const PpuBatch&) const=default;
};
struct PpuRasterWork {std::uint64_t decodes{},colour_updates{};};

// CPU decodes only cartridge-authored 2D artwork. GPU projection, native model
// depth and both eye views stay separate; this never reprojects a finished
// world image. One owner per painter group, not one per eye.
class PicaRaster {
public:
    // Immutable PPU storage must outlive preparation through the shared owner.
    // Spans remain valid until the next successful prepare; failures retain
    // the complete previous layer. Scenery owns 32px horizontal guard coverage.
    PicaFrame prepare(std::shared_ptr<const simulation::SnesPpuState>,const PpuBatch&,
        const FramePlan&,unsigned brightness=15,unsigned bg2_subtract=0);
    [[nodiscard]] PpuRasterWork work() const noexcept {return work_;}
private:
    std::shared_ptr<const simulation::SnesPpuState> source_;
    PpuBatch batch_;
    std::unique_ptr<render::Framebuffer> indexed_;
    std::vector<std::uint8_t> rgba_;
    std::vector<std::uint8_t> layers_;
    std::array<PicaVertex,6> vertices_{};
    std::array<PicaDraw,1> draws_{};
    std::array<PicaImage,1> images_{};
    std::array<std::uint16_t,256> palette_{};
    unsigned brightness_{},subtract_{};
    bool visible_{};
    PpuRasterWork work_;
};
} // namespace starfox::platform::nintendo_3ds

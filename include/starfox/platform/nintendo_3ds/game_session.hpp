#pragma once
#include "starfox/audio/spc700_audio.hpp"
#include "starfox/assets/shape_decoder.hpp"
#include "starfox/platform/nintendo_3ds/game_hud.hpp"
#include "starfox/platform/nintendo_3ds/game_presentation.hpp"
#include "starfox/vr/game_scene.hpp"
#include <functional>
#include <unordered_set>

namespace starfox::platform::nintendo_3ds {
struct GameAdvance {
    unsigned video_phases{},logic_ticks{},audio_blocks{};
    double raster_fraction{};
    bool duplicate{},time_clamped{};
    // The host must replace the cartridge owner, not run EX against Original
    // data (or vice versa). No further ticks are accepted while this is pending.
    std::optional<simulation::Experience> requested_experience;
};
// Graphics/SDK-independent native game owner. This is the actual simulation,
// SPC driver and cartridge HUD, not the asset-free frontend diagnostic. Native
// PICA/NDSP adapters consume its output; neither eye is allowed to tick it.
class GameSession {
public:
    // Consume/copy PCM synchronously: the span is reused after the callback.
    // The NDSP adapter must copy into a free DSP-owned linear-memory block.
    using PcmSink=std::function<void(std::span<const std::int16_t>)>;
    GameSession(assets::RomImage,assets::SymbolMap,PcmSink,
        std::string initial_map="BOOT",std::span<const std::uint8_t> cartridge_ram={});
    GameSession(const GameSession&)=delete;
    GameSession& operator=(const GameSession&)=delete;
    GameSession(GameSession&&)=delete; // Internal source references must stay stable.
    GameSession& operator=(GameSession&&)=delete;

    // Monotonic nanoseconds from the platform clock, sampled ONCE per host
    // frame. Source raster is 60 Hz; SPC is 20 Hz even at original FX pacing.
    GameAdvance advance(std::int64_t nanoseconds,input::ButtonMask held,bool focused=true);
    [[nodiscard]] GamePresentation presentation(float slider,bool stereoscopic_hardware,
        const StereoSettings& settings={}) const;
    [[nodiscard]] const simulation::GameSimulation& game() const noexcept {return game_;}
    [[nodiscard]] const audio::Spc700Audio& audio() const noexcept {return audio_;}
    // Immutable assets for native renderer owners; neither exposes VM mutation.
    [[nodiscard]] const assets::RomImage& rom() const noexcept {return rom_;}
    [[nodiscard]] const assets::SymbolMap& symbols() const noexcept {return symbols_;}
    [[nodiscard]] simulation::Experience cartridge_experience() const noexcept {return cartridge_experience_;}
    [[nodiscard]] std::span<const std::uint8_t> cartridge_ram() const noexcept {return game_.ex_save_ram();}
private:
    void prepare_pace_shapes();
    void publish_raster();
    assets::RomImage rom_;
    assets::SymbolMap symbols_;
    simulation::Experience cartridge_experience_;
    // The counts outlive the simulation's non-owning pace-table pointer.
    std::unordered_map<std::uint32_t,std::uint32_t> face_counts_;
    std::unordered_set<std::uint32_t> invalid_shapes_;
    assets::ShapeDecoder decoder_;
    simulation::GameSimulation game_;
    audio::Spc700Audio audio_;
    PcmSink sink_;
    GameHud hud_;
    GameHudFrame hud_frame_;
    vr::GameSceneHistory history_;
    std::shared_ptr<const GameRasterSnapshot> raster_;
    timing::FixedStepClock clock_{60};
    input::InputLatch input_;
    std::optional<std::int64_t> previous_time_;
    std::vector<simulation::ApuPortWrite> pending_audio_;
    std::vector<std::int16_t> mixed_;
    unsigned audio_phase_{};
    double fraction_{};
    bool failed_{},suppress_held_{};
    std::optional<simulation::Experience> requested_experience_;
};
} // namespace starfox::platform::nintendo_3ds

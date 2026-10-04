#include "starfox/platform/nintendo_3ds/game_session.hpp"
#include "starfox/audio/stem_mixer.hpp"
#include "starfox/platform/nintendo_3ds/audio_pcm.hpp"
#include <cctype>

namespace starfox::platform::nintendo_3ds {
static_assert(AudioPcm::rate==audio::Spc700Audio::sample_rate
    && AudioPcm::frames==audio::Spc700Audio::stereo_frames_per_logic_tick);
namespace {
input::ButtonMask gameplay_buttons(input::ButtonMask held,bool swap) noexcept {
    if(!swap) return held;
    const auto pair=[&](input::ButtonMask a,input::ButtonMask b) {
        const auto before=held;
        held=static_cast<input::ButtonMask>(held&~(a|b));
        if(before&a) held|=b;
        if(before&b) held|=a;
    };
    pair(input::a,input::b);pair(input::x,input::y);return held;
}
}
GameSession::GameSession(assets::RomImage rom,assets::SymbolMap symbols,PcmSink sink,
    std::string initial_map,std::span<const std::uint8_t> cartridge_ram)
    :rom_(std::move(rom)),symbols_(std::move(symbols)),
     cartridge_experience_(symbols_.find("SPECWEPCNTONE").empty()
         ?simulation::Experience::original:simulation::Experience::starfox_ex),
     decoder_(rom_,symbols_),game_(rom_,symbols_,initial_map,cartridge_ram,true),
     sink_(std::move(sink)),hud_(rom_,symbols_),history_(game_,rom_,symbols_,vr::SceneCameraPolicy::source) {
    if(!sink_) throw std::invalid_argument("3DS game requires a PCM consumer");
    game_.set_experience(cartridge_experience_);
    game_.set_timing_mode(simulation::TimingMode::original_speed);
    game_.set_shape_face_counts(&face_counts_);
    // Native cartridge audio remains available; MSU support must not be
    // advertised without a decoder/streaming adapter connected to this owner.
    game_.set_msu1_available(false);
    const auto boot_writes=game_.map().take_apu_port_writes();
    if(!boot_writes.empty()) {
        static_cast<void>(audio_.prime_upload_sequence(boot_writes));
        for(auto& c:initial_map) c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        // Match the ordinary runtime's base-driver initialization before a
        // direct stage-bank overlay. Do not queue inaudible startup preroll.
        if(initial_map!="BOOT") for(unsigned tick=0;tick<30;++tick)
            static_cast<void>(audio_.render_logic_tick({}));
        game_.synchronize_apu_output_ports(audio_.output_ports());
    }
    history_.capture();history_.reset_interpolation();publish_raster();
}
void GameSession::prepare_pace_shapes() {
    if(game_.timing_mode()!=simulation::TimingMode::original_speed) return;
    // Same source draw list/shape counts as desktop, before the pace decision;
    // never estimate original timing from the current eye's culling or GPU work.
    for(auto handle:game_.draw_order()) {
        if(!game_.objects().is_active(handle)) continue;
        const auto shape=game_.objects().at(handle).shape;
        if(face_counts_.contains(shape) || invalid_shapes_.contains(shape)) continue;
        try {face_counts_.emplace(shape,static_cast<std::uint32_t>(decoder_.decode(shape).faces.size()));}
        catch(const std::exception&) {invalid_shapes_.insert(shape);}
    }
}
void GameSession::publish_raster() {
    auto next=std::make_shared<GameRasterSnapshot>();
    const auto& ppu=game_.map().ppu_state();
    // At most one immutable PPU copy per host advance, never per eye. Reuse
    // unchanged video storage and completed-model snapshots where possible.
    if(raster_ && *raster_->ppu==ppu) next->ppu=raster_->ppu;
    else if(history_.current()->ppu && *history_.current()->ppu==ppu) next->ppu=history_.current()->ppu;
    else next->ppu=std::make_shared<const simulation::SnesPpuState>(ppu);
    next->brightness=game_.map().display_brightness();
    next->circle=game_.circle_effect_state();next->wipe=game_.window_wipe_state();
    next->colour_math=game_.colour_math_effect_state();
    next->boss_roll=game_.boss_roll_active();
    next->stage_hud=game_.stage_results_state().visible;
    hud_frame_=hud_.capture(game_);hud_.update(hud_frame_);
    raster_=std::move(next);
}
GameAdvance GameSession::advance(std::int64_t time,input::ButtonMask held,bool focused) {
    if(failed_) throw std::runtime_error("Reconstruct 3DS game after a failed source/audio tick");
    if(time<0) throw std::invalid_argument("Invalid 3DS monotonic frame time");
    GameAdvance result;result.requested_experience=requested_experience_;
    if(requested_experience_) return result;
    if(!focused) {
        previous_time_.reset();clock_.reset();input_.reset();fraction_=0;
        suppress_held_=true;history_.reset_interpolation();return result;
    }
    // Home/sleep/resume must not act as a fresh held Start/A press. Wait for
    // release; pending pre-suspend APU events/partial 20 Hz cadence survive.
    if(suppress_held_) {if(!held) suppress_held_=false;else held=0;}
    if(!game_.in_setup_menu()) held=gameplay_buttons(held,game_.swap_face_buttons());
    input_.sample(held); // Also retain quick input on a duplicate display time.
    if(previous_time_ && time==*previous_time_) {
        result.duplicate=true;result.raster_fraction=fraction_;return result;
    }
    if(!previous_time_ || time<*previous_time_) {
        const bool rewound=previous_time_ && time<*previous_time_;
        previous_time_=time;clock_.reset();fraction_=0;history_.reset_interpolation();
        if(rewound) {input_.reset();suppress_held_=held!=0;}
        return result;
    }
    const auto batch=clock_.advance(std::chrono::nanoseconds(time-*previous_time_));previous_time_=time;
    result.time_clamped=batch.time_was_clamped;
    fraction_=result.raster_fraction=batch.interpolation_alpha;
    try {
        for(unsigned phase=0;phase<batch.simulation_steps;++phase) {
            prepare_pace_shapes();game_.present_frame();++result.video_phases;
            if(game_.logic_tick_ready()) {
                const bool runtime=game_.runtime_options_open(),paused=game_.paused();
                const auto tick=game_.tick(input_.consume());++result.logic_ticks;
                if(!runtime) pending_audio_.insert(pending_audio_.end(),
                    tick.audio_port_writes.begin(),tick.audio_port_writes.end());
                // No MSU consumer is enabled. Retire the unused register stream
                // rather than allowing an unbounded vector on original hardware.
                static_cast<void>(game_.map().take_msu_register_writes());
                if(runtime && !game_.runtime_options_open()) input_.reset();
                history_.capture();
                if(paused || game_.paused()) history_.reset_interpolation();
                if(game_.experience()!=cartridge_experience_) {
                    requested_experience_=game_.experience();
                    result.requested_experience=requested_experience_;
                    clock_.reset();fraction_=result.raster_fraction=0;
                    history_.reset_interpolation();break;
                }
            }
            if(!game_.runtime_options_open() && ++audio_phase_==3) {
                static_cast<void>(audio_.render_logic_tick(pending_audio_));
                audio::mix_stems(audio_.last_music_samples(),audio_.last_effect_samples(),
                    game_.music_volume(),game_.sfx_volume(),mixed_);
                sink_(mixed_);game_.synchronize_apu_output_ports(audio_.output_ports());
                pending_audio_.clear();audio_phase_=0;++result.audio_blocks;
            }
        }
        if(result.video_phases) {
            if(history_.current()->scene_epoch!=game_.scene_revision()
                || history_.current()->flow!=game_.flow_state()) {
                history_.capture();history_.reset_interpolation();
            }
            publish_raster();
        }
    } catch(...) {failed_=true;throw;}
    return result;
}
GamePresentation GameSession::presentation(float slider,bool hardware,const StereoSettings& settings) const {
    if(failed_) throw std::runtime_error("Cannot present a failed 3DS game session");
    return {plan_frame(slider,hardware,hud_frame_.routing.screen,settings),
        history_.previous(),history_.current(),raster_,game_.logic_interpolation_alpha(fraction_),
        GameHud::top_selection(hud_frame_),hud_.view()};
}
} // namespace starfox::platform::nintendo_3ds

#include "starfox/platform/nintendo_3ds/game_session.hpp"
#include "starfox/audio/stem_mixer.hpp"
#include "starfox/platform/nintendo_3ds/audio_pcm.hpp"
#include <bit>
#include <iostream>

namespace {
using namespace starfox;
using namespace platform::nintendo_3ds;
unsigned checks{};
void require(bool value,const char* message) {++checks;if(!value) throw std::runtime_error(message);}
template<class F> void rejects(F f,const char* message) {
    bool rejected=false;try {f();} catch(const std::exception&) {rejected=true;}
    require(rejected,message);
}
std::int64_t timestamp(unsigned frame,unsigned fps=60) {
    return (std::int64_t(frame)*1'000'000'000+fps-1)/fps;
}
// Independent cartridge oracle: direct native raster phases, not GameSession,
// FixedStepClock, GameSceneHistory or either presentation-eye implementation.
struct SourceOracle {
    assets::ShapeDecoder decoder;
    std::unordered_map<std::uint32_t,std::uint32_t> counts;
    std::unordered_set<std::uint32_t> invalid;
    simulation::GameSimulation game;
    audio::Spc700Audio spc;
    input::InputLatch input;
    unsigned phase{},ticks{},blocks{};
    std::vector<std::int16_t> pcm;
    SourceOracle(const assets::RomImage& rom,const assets::SymbolMap& symbols,const std::string& map)
        :decoder(rom,symbols),game(rom,symbols,map,{},true) {
        game.set_experience(symbols.find("SPECWEPCNTONE").empty()
            ?simulation::Experience::original:simulation::Experience::starfox_ex);
        game.set_timing_mode(simulation::TimingMode::original_speed);game.set_shape_face_counts(&counts);
        game.set_msu1_available(false); // Same platform capability, not source timing.
        const auto uploads=game.map().take_apu_port_writes();
        if(!uploads.empty()) {
            static_cast<void>(spc.prime_upload_sequence(uploads));
            if(map!="BOOT") for(unsigned i=0;i<30;++i) static_cast<void>(spc.render_logic_tick({}));
            game.synchronize_apu_output_ports(spc.output_ports());
        }
    }
    std::vector<simulation::ApuPortWrite> pending;
    void raster() {
        for(auto handle:game.draw_order()) if(game.objects().is_active(handle)) {
            const auto id=game.objects().at(handle).shape;
            if(counts.contains(id) || invalid.contains(id)) continue;
            try {counts.emplace(id,std::uint32_t(decoder.decode(id).faces.size()));}
            catch(const std::exception&) {invalid.insert(id);}
        }
        game.present_frame();
        if(game.logic_tick_ready()) {
            const auto tick=game.tick(input.consume());++ticks;
            pending.insert(pending.end(),tick.audio_port_writes.begin(),tick.audio_port_writes.end());
            static_cast<void>(game.map().take_msu_register_writes());
        }
        if(++phase%3==0) {
            static_cast<void>(spc.render_logic_tick(pending));pending.clear();
            std::vector<std::int16_t> next;
            audio::mix_stems(spc.last_music_samples(),spc.last_effect_samples(),
                game.music_volume(),game.sfx_volume(),next);
            pcm.insert(pcm.end(),next.begin(),next.end());
            game.synchronize_apu_output_ports(spc.output_ports());++blocks;
        }
    }
};
void parity(const assets::RomImage& rom,const assets::SymbolMap& symbols,const std::string& map) {
    std::vector<std::int16_t> pcm;
    unsigned blocks{},phases{},ticks{},raster_only_updates{};
    GameSession session(rom,symbols,[&](auto samples) {
        require(samples.size()==AudioPcm::samples,"SPC block is not exactly 50ms of stereo PCM");
        std::array<std::int16_t,AudioPcm::samples> copied{};
        copy_audio_pcm(samples,copied); // Same copy contract as the real NDSP sink.
        pcm.insert(pcm.end(),copied.begin(),copied.end());++blocks;
    },map);
    SourceOracle source(rom,symbols,map);
    require(session.game().save_state()==source.game.save_state(),"Initial native owner changed cartridge state");
    require(session.audio().save_state()==source.spc.save_state(),"Initial SPC bank/preroll differs from source");
    if(map=="BOOT") {
        require(session.game().flow_state()==simulation::GameFlowState::pregame_menu,"Real pre-game menu was skipped");
        const auto frame=session.presentation(1,true);
        require(!frame.plan.stereo && frame.sprites==render::SpriteSelection::all,"Setup text/artwork was routed into gameplay");
    }
    require(session.game().timing_mode()==simulation::TimingMode::original_speed,"Original FX timing not baseline");
    require(blocks==0,"Silent bank initialization queued startup audio");
    const auto initial_snapshot=session.presentation(0,true).current;
    const auto initial_raster=session.presentation(0,true).raster;
    const auto initial_native_ppu=*initial_raster->ppu;
    const auto initial_vram=initial_snapshot->ppu->vram;
    const auto initial_palette=initial_snapshot->cgram;
    session.advance(0,0);
    for(unsigned frame=1;frame<=720;++frame) {
        // 240 Hz polling with one immutable scene for any number of eye reads.
        // A short Start tap at 133ms must reach the source tick from the latch.
        const input::ButtonMask held=map=="BOOT" && frame==32?input::start
            :map!="BOOT" && frame>=120 && frame<400?input::ButtonMask(input::y|input::up|input::right):0;
        source.input.sample(held);
        const auto result=session.advance(timestamp(frame,240),held);
        phases+=result.video_phases;ticks+=result.logic_ticks;
        require(!result.time_clamped && !result.requested_experience,"Normal source clock clamped/switched cartridge");
        if(frame%4==0) {
            source.raster();
            const auto published=session.presentation(0,true);
            require(*published.raster->ppu==source.game.map().ppu_state()
                && published.raster->brightness==source.game.map().display_brightness(),
                "Native video/fade was delayed until a slower model tick");
            if(published.current->display_brightness!=published.raster->brightness) ++raster_only_updates;
        }
        if(frame%120==0) {
            require(phases==frame/4,"High presentation rate altered native 60Hz raster count");
            require(ticks==source.ticks && blocks==source.blocks,"Source/20Hz audio cadence differs");
            require(session.game().save_state()==source.game.save_state(),"3DS source state differs from independent native phases");
            require(session.audio().save_state()==source.spc.save_state() && pcm==source.pcm,"SPC PCM/handshakes differ");
            const auto old= session.presentation(0,true);
            const auto source_coordinate=[&](const char* name) {
                for(auto address:symbols.find(name)) if(address>>16==0 || address>>16==0x7e)
                    return std::bit_cast<std::int16_t>(session.game().map().peek_ram_word(address).value());
                throw std::runtime_error("Camera coordinate missing in cartridge fixture");
            };
            require(old.current->camera.x==source_coordinate("VIEWPOSX")
                && old.current->camera.y==source_coordinate("VIEWPOSY")
                && old.current->camera.z==source_coordinate("VIEWPOSZ"),"Console snapshot inherited a headset-only camera adjustment");
            const auto game_before=session.game().save_state(),apu_before=session.audio().save_state();
            for(float slider:{0.F,.2F,.5F,1.F,0.F}) {
                const auto plan=session.presentation(slider,true);
                require(plan.current==old.current && plan.previous==old.previous && plan.raster==old.raster,
                    "Slider/eyes published a different game or display tick");
                require(valid_image(plan.dashboard,bottom_width,screen_height),"Cartridge lower HUD is invalid");
                require(plan.plan.eye_count==(plan.plan.stereo?2U:1U),"Physical slider eye count mismatch");
            }
            require(session.game().save_state()==game_before && session.audio().save_state()==apu_before,"Eye projection mutated game/audio");
            require(!session.presentation(1,false).plan.stereo,"Mono hardware fabricated a second eye");
        }
    }
    require(phases==180 && blocks==60,"Three seconds did not retain source/audio cadence");
    require(initial_snapshot->ppu->vram==initial_vram && initial_snapshot->cgram==initial_palette,
        "Retained stereo scene referenced mutable cartridge video memory");
    require(*initial_raster->ppu==initial_native_ppu,"Retained native raster referenced mutable cartridge video memory");
    if(map=="BOOT") require(raster_only_updates>0,"Fade fixture never exercised native raster updates between model ticks");
    if(map=="BOOT") require(session.game().flow_state()!=simulation::GameFlowState::pregame_menu,"Quick physical Start tap was lost");
    if(session.game().flow_state()==simulation::GameFlowState::gameplay)
        require(session.presentation(1,true).sprites==render::SpriteSelection::world_only,"Gameplay source HUD was not partitioned");
    // Suspend with a partially accumulated 50ms audio block, not just at its
    // convenient three-raster boundary. Resume must not discard that cadence.
    session.advance(timestamp(728,240),0);
    source.input.sample(0);source.raster();source.raster();
    const auto retained=session.presentation(1,true).current;
    const auto saved=session.game().save_state(),saved_audio=session.audio().save_state();
    const auto duplicate=session.advance(timestamp(728,240),0);
    require(duplicate.duplicate && !duplicate.video_phases && !duplicate.audio_blocks,"Duplicate time ticked source/audio");
    rejects([&]{session.advance(-1,0);},"Negative time accepted");
    session.advance(timestamp(729,240),input::start,false);
    session.advance(900'000'000'000LL,input::start,true);
    require(session.game().save_state()==saved && session.audio().save_state()==saved_audio,"Suspend/resume advanced or reprised source state");
    require(session.presentation(1,true).previous==retained,"Resume retained an interpolated old pose");
    // All buttons are released on resume before accepting a fresh Start.
    session.advance(900'000'000'001LL,0,true);
    const auto clamped=session.advance(901'000'000'001LL,0,true);
    require(clamped.time_clamped && clamped.video_phases==15 && clamped.audio_blocks==5,"Long stall not bounded to 250ms of native phases");
    source.input.reset();for(unsigned i=0;i<15;++i) source.raster();
    require(session.game().save_state()==source.game.save_state() && pcm==source.pcm,"Suspension or clamped catch-up changed cartridge/audio");
    const auto last=session.advance(901'000'000'001LL+timestamp(1),0);
    source.raster();
    require(last.video_phases==1 && last.audio_blocks==1 && blocks==66
        && session.game().save_state()==source.game.save_state() && pcm==source.pcm,
        "Suspend/resume discarded a partial source audio block");
    const auto rewind=session.advance(100,0);
    require(!rewind.video_phases && session.presentation(1,true).previous==session.presentation(1,true).current,"Clock rewind replayed a source pose");
    std::cout<<"  "<<map<<": 198 source rasters, "<<ticks<<" initial logic ticks, 66 exact SPC blocks; state/PCM oracle matches\n";
}
void handoff(const assets::RomImage& rom,const assets::SymbolMap& symbols) {
    GameSession session(rom,symbols,[](auto){ });
    const auto cartridge=session.cartridge_experience();
    session.advance(0,0);
    require(session.advance(0,input::a).duplicate && session.advance(0,0).duplicate,
        "Duplicate time did not preserve quick physical menu samples");
    const auto result=session.advance(50'000'000,0);
    require(result.requested_experience && *result.requested_experience!=cartridge,"Experience row did not request a cartridge handoff");
    const auto game=session.game().save_state(),apu=session.audio().save_state();
    const auto next=session.advance(5'000'000'000LL,input::start);
    require(next.requested_experience && !next.video_phases && !next.audio_blocks,"Wrong cartridge kept running after experience selection");
    require(game==session.game().save_state() && apu==session.audio().save_state(),"Pending cartridge handoff changed source/audio");
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=3) throw std::invalid_argument("Usage: game_session_check ROM SYMBOLS");
        const auto rom=assets::RomImage::load(argv[1]);const auto symbols=assets::SymbolMap::load(argv[2]);
        parity(rom,symbols,"BOOT");parity(rom,symbols,"LEVEL1_1");handoff(rom,symbols);
        GameSession failed(rom,symbols,[](auto){throw std::runtime_error("PCM device failed");});
        failed.advance(0,0);rejects([&]{failed.advance(50'000'000,0);},"PCM failure ignored");
        rejects([&]{failed.advance(100'000'000,0);},"Failed source tick retried against partly advanced state");
        rejects([&]{static_cast<void>(failed.presentation(0,true));},"Failed audio/source session presented stale geometry");
        std::cout<<"3DS actual game session: "<<checks<<" checks passed; host source parity, not PICA/NDSP or hardware acceptance\n";
    } catch(const std::exception& error) {std::cerr<<"3DS actual game session: "<<error.what()<<'\n';return 1;}
}

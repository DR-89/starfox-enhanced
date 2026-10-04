#include "starfox/platform/nintendo_3ds/game_session.hpp"
#include "starfox/audio/stem_mixer.hpp"
#include "starfox/platform/nintendo_3ds/audio_pcm.hpp"
#include "starfox/platform/nintendo_3ds/game_menu.hpp"
#include "starfox/state/container.hpp"
#include "starfox/state/archive.hpp"
#include "starfox/assets/bps.hpp"
#include "starfox/platform/nintendo_3ds/game_models.hpp"
#include "starfox/platform/nintendo_3ds/game_layers.hpp"
#include "starfox/platform/nintendo_3ds/pica_composite.hpp"
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
    const auto initial_owner_state=session.game().save_state(),initial_source_state=source.game.save_state();
    if(initial_owner_state!=initial_source_state) {
        const auto a=state::unpack(initial_owner_state,0x47414d01U,assets::crc32(rom.bytes()));
        const auto b=state::unpack(initial_source_state,0x47414d01U,assets::crc32(rom.bytes()));
        std::cerr<<"Initial state payload bytes owner/source: "<<a.size()<<'/'<<b.size()<<"; differing offsets:";
        unsigned shown=0;
        for(std::size_t i=0;i<std::min(a.size(),b.size()) && shown<12;++i)
            if(a[i]!=b[i]) {std::cerr<<' '<<i;++shown;}
        std::cerr<<'\n';
    }
    require(initial_owner_state==initial_source_state,"Initial native owner changed cartridge state");
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
struct MenuDriver {
    GameSession& session;std::int64_t time{};GameAdvance last;
    explicit MenuDriver(GameSession& source):session(source) {session.advance(0,0);}
    void tap(input::ButtonMask button) {
        time+=50'000'000;last=session.advance(time,button);
        time+=50'000'000;const auto release=session.advance(time,0);
        if(release.requested_experience || release.requested_preview) last=release;
    }
    void select(unsigned id) {
        const auto order=simulation::pregame_menu_order(session.game().pregame_page());
        require(std::find(order.begin(),order.end(),id)!=order.end(),"Fixture selected a row absent from source order");
        for(std::size_t i=0;session.game().pregame_selection()!=id && i<order.size();++i) tap(input::down);
        require(session.game().pregame_selection()==id,"Actual menu navigation did not reach requested source row");
    }
};
void actual_menu(const assets::RomImage& rom,const assets::SymbolMap& symbols,const std::filesystem::path& captures) {
    GameSession session(rom,symbols,[](auto){});MenuDriver controls(session);
    GameMenu menu(rom,symbols);
    const auto observe=[&] {
        const auto before=session.game().save_state(),spc=session.audio().save_state();
        const auto state=GameMenu::capture(session.game());menu.update(state);
        const auto order=simulation::pregame_menu_order(session.game().pregame_page());
        require(state.rows.size()==order.size(),"Native UI replaced full source menu with a reduced menu");
        for(std::size_t i=0;i<order.size();++i) require(state.rows[i].id==order[i] && !state.rows[i].label.empty(),"Source menu row was missing/reordered/unlabelled");
        require(state.selection==session.game().pregame_selection(),"Rendered cursor was detached from source navigation");
        const auto source=session.presentation(1,true);validate_pica_frame(menu.frame(source.plan),source.dashboard);
        require(before==session.game().save_state() && spc==session.audio().save_state(),"Menu/font capture mutated source VM/SPC state");
        const auto redraws=menu.redraws();require(!menu.update(state) && menu.redraws()==redraws,"Unchanged source menu was rasterized again");
        return state;
    };
    observe();
    if(!captures.empty()) {
        std::filesystem::create_directories(captures);Canvas canvas(top_width);canvas.image(0,0,menu.plain_view());
        canvas.write_bmp((captures/"actual-main-menu.bmp").string());
    }
    controls.select(2);
    const auto fps=session.game().presentation_fps();controls.tap(input::a);
    require(session.game().presentation_fps()==fps,"Unavailable 3DS FPS target was changed anyway");
    controls.select(14);controls.tap(input::a);
    require(session.game().pregame_page()==simulation::PregamePage::options,"Source Options action not used");observe();
    controls.select(6);const auto volume=session.game().music_volume();controls.tap(input::left);
    require(session.game().music_volume()<volume,"Source music volume did not change");observe();
    controls.select(12);controls.tap(input::right);
    require(session.game().language()==1,"Actual source language did not change");observe();
    controls.select(5);controls.tap(input::a);
    require(session.game().swap_face_buttons(),"Actual face-swap setting did not change");observe();
    controls.select(9);controls.tap(input::a);
    require(session.game().pregame_page()==simulation::PregamePage::stereo,"Source stereo submenu did not open");observe();
    controls.select(1);const auto separation=session.stereo_settings().separation;controls.tap(input::right);
    require(session.stereo_settings().separation>separation,"Native projection did not consume source separation control");observe();
    controls.select(2);const auto convergence=session.stereo_settings().convergence;controls.tap(input::right);
    require(session.stereo_settings().convergence>convergence,"Native projection did not consume source convergence control");observe();
    const auto optical=session.presentation(1,true,session.stereo_settings());
    require(!optical.plan.stereo && optical.plan.eye_count==1
        && optical.plan.convergence==session.stereo_settings().convergence,"Plain setup text acquired world stereo disparity");
    controls.tap(input::b);
    require(session.game().pregame_page()==simulation::PregamePage::options,"Source stereo Back failed");
    controls.select(0);controls.tap(input::a);
    require(session.game().pregame_page()==simulation::PregamePage::cheats,"Source Cheats action not used");
    for(auto id:simulation::pregame_menu_order(simulation::PregamePage::cheats)) {controls.select(id);observe();}
    controls.select(1);controls.tap(input::right);
    require(session.game().selected_level()==11,"Source level-selection list not used");
    controls.select(3);controls.tap(input::a);
    require(session.game().infinite_bombs(),"Source infinite-bombs action not used");
    controls.tap(input::b);controls.tap(input::b);
    require(session.game().pregame_page()==simulation::PregamePage::main,"Actual menu Back transition failed");
    for(unsigned page_row:{20U,21U,47U}) {
        controls.select(page_row);controls.tap(input::a);observe();
        const auto order=simulation::pregame_menu_order(session.game().pregame_page());
        for(auto id:order) {controls.select(id);observe();}
        controls.select(order.front());const auto state=observe();controls.tap(input::a);
        require(GameMenu::capture(session.game())==state,"Unavailable PICA effect silently changed its source setting");
        controls.tap(input::b);
        require(session.game().pregame_page()==simulation::PregamePage::main,"Graphics-page source Back failed");
    }
    const auto prefs=session.preferences();
    controls.select(16);controls.tap(input::a);
    require(controls.last.requested_preview==true && !controls.last.start_after_preview,"Preview did not request a real stage-owner restart");
    const auto frozen=session.game().save_state(),frozen_spc=session.audio().save_state();
    const auto pending=session.advance(controls.time+1'000'000'000,0);
    require(pending.requested_preview==true && !pending.video_phases && !pending.audio_blocks
        && frozen==session.game().save_state() && frozen_spc==session.audio().save_state(),"Pending preview ticked the old cartridge/audio");
    GameSessionOptions options;options.preferences=prefs;options.preview=true;unsigned progress{};
    options.preview_progress=[&](unsigned){++progress;return true;};
    const auto source_ram=session.cartridge_ram();
    require(session.cartridge_experience()==simulation::Experience::starfox_ex || source_ram.empty(),
        "Retail generic VM RAM was misidentified as battery-backed SRAM");
    const std::vector<std::uint8_t> saved_ram(source_ram.begin(),source_ram.end());
    GameSession preview(rom,symbols,[](auto){},"LEVEL1_1",saved_ram,options);MenuDriver preview_controls(preview);
    require(progress>0 && preview.preferences()==prefs,"Preview lost settings or skipped bounded source preroll");
    require(std::equal(preview.cartridge_ram().begin(),preview.cartridge_ram().end(),saved_ram.begin(),saved_ram.end()),
        "Preview source preroll changed the user's preserved cartridge SRAM");
    require(preview.game().menu_preview() && preview.game().peek_meter_state().enabled,"Preview froze the empty source initializer instead of gameplay");
    GameMenu preview_menu(rom,symbols);preview_menu.update(GameMenu::capture(preview.game()));
    const auto scene=preview.presentation(1,true,preview.stereo_settings());
    require(scene.plan.stereo,"Hardware slider was ignored in the real menu preview");
    require(scene.plan.separation==preview.stereo_settings().separation
        && scene.plan.convergence==preview.stereo_settings().convergence,"Configured native depth detached from preview eye matrices");
    GameModels models(preview.rom(),preview.symbols());GameLayers layers;PicaComposite composite;
    const auto shapes=models.prepare(scene);
    const auto art=layers.prepare(scene);
    const auto frame=composite.prepare(scene.plan,std::array{art.before_models,shapes,art.after_models,preview_menu.frame(scene.plan)},scene.dashboard,art.clear);
    validate_pica_frame(frame,scene.dashboard);
    require(!shapes.vertices.empty() && !art.before_models.textures.empty(),"Preview substituted a mono placeholder for real PICA models/backgrounds");
    const auto game=preview.game().save_state(),apu=preview.audio().save_state();
    for(float slider:{0.F,.5F,1.F}) {
        const auto eye=preview.presentation(slider,true);
        preview_menu.update(GameMenu::capture(preview.game()));validate_pica_frame(preview_menu.frame(eye.plan),eye.dashboard);
    }
    require(game==preview.game().save_state() && apu==preview.audio().save_state(),"Preview/slider changed frozen cartridge state");
    preview_controls.tap(input::start);
    require(preview_controls.last.requested_preview==false && preview_controls.last.start_after_preview,"Preview Start did not request the real BOOT/Start path");
    options.preview=false;options.start_after_preview=true;
    GameSession started(rom,symbols,[](auto){},"BOOT",{},options);started.advance(0,0);
    for(unsigned phase=1;phase<=180;++phase) started.advance(timestamp(phase),0);
    require(!started.game().in_setup_menu() && started.preferences()==prefs,"Preview Start failed to launch through source fade/selected level");
    options.preview=true;options.start_after_preview=false;options.preview_progress=[](unsigned){return false;};
    rejects([&]{GameSession cancelled(rom,symbols,[](auto){},"LEVEL1_1",{},options);},"Cancelled preview was published as a playable owner");
    std::cout<<"  Actual setup: all source pages/rows, font/cache/protected UI, real preview geometry, settings and Start handoff checked\n";
}
void merged_menu_compatibility(const assets::RomImage& rom,const assets::SymbolMap& symbols) {
    simulation::GameSimulation game(rom,symbols,"BOOT",{},true);
    game.set_experience(symbols.find("SPECWEPCNTONE").empty()
        ?simulation::Experience::original:simulation::Experience::starfox_ex);
    const auto tap=[&](input::ButtonMask button) {
        static_cast<void>(game.tick({button,button,0}));static_cast<void>(game.tick({}));
    };
    const auto select=[&](unsigned id) {
        const auto order=simulation::pregame_menu_order(game.pregame_page());
        for(std::size_t i=0;game.pregame_selection()!=id && i<order.size();++i) tap(input::down);
        require(game.pregame_selection()==id,"Merged graphics row disappeared");
    };
    select(21);tap(input::a);select(79);
    require(game.pregame_page()==simulation::PregamePage::three_d,"Merged asteroid row moved to wrong page");
    for(unsigned mode=1;mode<=4;++mode) {
        tap(input::right);
        require(static_cast<unsigned>(game.asteroid_models())==mode%4,"Merged asteroid action was lost");
        require(game.aa_type()==0,"Asteroid row also changed AA type");
    }
    game.set_asteroid_models(2);select(42);tap(input::right);
    require(game.aa_type()==1 && static_cast<unsigned>(game.asteroid_models())==2,
        "AA TYPE still collides with merged asteroid control");
    game.configure_neural_filter(true,true);
    require(!game.neural_filter_available() && !game.neural_filter_requested(),"Retired neural/ReShade path was re-enabled");
    require(simulation::GameSimulation::constrain_renderer_mode(simulation::RendererMode::software,true)
        ==simulation::RendererMode::gpu,"PS5 hardware-only renderer constraint was lost");
    require(simulation::GameSimulation::constrain_renderer_mode(simulation::RendererMode::software,false)
        ==simulation::RendererMode::software,"Ordinary renderer selection became hardware-only");
    const auto saved=game.save_state();
    const auto restored=game.restored_state(saved);
    require(restored->aa_type()==1 && static_cast<unsigned>(restored->asteroid_models())==2,
        "State restore lost merged device asteroid/AA settings");
    // Independently encode precisely the new optional tail, then construct the
    // old branch's archive shape. Do not guess an offset in the VM payload.
    state::Writer tail;
    tail(game.aa_type(),game.integer_scaling(),std::uint8_t(2),game.extra_effects(),
        game.global_enhancements(),game.scene_enhancements(),game.depth_enhancements(),
        game.particle_enhancements(),game.phosphor_persistence(),game.adaptive_exposure(),
        game.water_caustics(),game.shadow_softness(),game.camera_response(),game.volumetric_fog(),
        game.stereo_separation(),game.stereo_convergence(),game.stereo_crosshair_depth(),game.motion_blur());
    const auto crc=assets::crc32(rom.bytes());const auto payload=state::unpack(saved,0x47414d01U,crc);
    require(payload.size()>tail.bytes().size() && std::equal(tail.bytes().rbegin(),tail.bytes().rend(),payload.rbegin()),
        "Legacy compatibility fixture no longer matches optional archive tail");
    const auto legacy=state::pack(0x47414d01U,crc,payload.first(payload.size()-tail.bytes().size()));
    const auto migrated=game.restored_state(legacy);
    require(migrated->pregame_selection()==79 && migrated->aa_type()==0
        && static_cast<unsigned>(migrated->asteroid_models())==2,
        "Legacy asteroid cursor restored as AA TYPE");
    GameMenu menu(rom,symbols);menu.update(GameMenu::capture(*migrated));
    require(menu.state().selection==79,"Legacy cursor no longer renders in full source menu");
    std::cout<<"  Shared menu compatibility: separate AA/asteroids, legacy cursor, PS5 constraint, retired neural path checked\n";
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=3 && !(argc==5 && std::string_view(argv[3])=="--capture"))
            throw std::invalid_argument("Usage: game_session_check ROM SYMBOLS [--capture DIRECTORY]");
        const auto rom=assets::RomImage::load(argv[1]);const auto symbols=assets::SymbolMap::load(argv[2]);
        parity(rom,symbols,"BOOT");parity(rom,symbols,"LEVEL1_1");handoff(rom,symbols);
        actual_menu(rom,symbols,argc==5?std::filesystem::path(argv[4]):std::filesystem::path{});
        merged_menu_compatibility(rom,symbols);
        GameSession failed(rom,symbols,[](auto){throw std::runtime_error("PCM device failed");});
        failed.advance(0,0);rejects([&]{failed.advance(50'000'000,0);},"PCM failure ignored");
        rejects([&]{failed.advance(100'000'000,0);},"Failed source tick retried against partly advanced state");
        rejects([&]{static_cast<void>(failed.presentation(0,true));},"Failed audio/source session presented stale geometry");
        std::cout<<"3DS actual game session: "<<checks<<" checks passed; host source parity, not PICA/NDSP or hardware acceptance\n";
    } catch(const std::exception& error) {std::cerr<<"3DS actual game session: "<<error.what()<<'\n';return 1;}
}

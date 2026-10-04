// Actual VM/SPC/HUD link and SD bring-up, NOT the completed game renderer.
#include "native_display.hpp"
#include "native_audio.hpp"
#include "companion_manifest.hpp"
#include "starfox/platform/nintendo_3ds/game_assets.hpp"
#include "starfox/platform/nintendo_3ds/game_session.hpp"
#include "starfox/platform/nintendo_3ds/game_models.hpp"
#include "starfox/platform/nintendo_3ds/game_menu.hpp"
#include "starfox/platform/nintendo_3ds/game_storage.hpp"
#include "starfox/assets/bps.hpp"
#if defined(STARFOX_3DS_CORE_PICA)
#include "native_gpu.hpp"
#include "pica_scene_shader.hpp"
#include "starfox/platform/nintendo_3ds/game_layers.hpp"
#include "starfox/platform/nintendo_3ds/pica_composite.hpp"
#include "starfox/platform/nintendo_3ds/pica_colour.hpp"
#include "starfox/platform/nintendo_3ds/pica_window.hpp"
#endif
#include <3ds.h>
#include <fstream>

namespace {
namespace ctr=starfox::platform::nintendo_3ds;
constexpr auto companion_path="sdmc:/3ds/starfox-enhanced/Starfox-Assets.BIN";
std::int64_t monotonic_time() {
    const auto ticks=svcGetSystemTick();
    // Split before multiplication: uptime must not overflow nanosecond math.
    return static_cast<std::int64_t>((ticks/SYSCLOCK_ARM11)*1'000'000'000ULL
        +((ticks%SYSCLOCK_ARM11)*1'000'000'000ULL)/SYSCLOCK_ARM11);
}
// libctru invokes APT hooks synchronously from aptMainLoop, on the owner thread.
// Preserve source/audio cadence but rebase input/time around Home and sleep.
class Suspension {
public:
    Suspension(ctr::GameSession& session,ctr::NativeAudio& audio,std::function<void()> checkpoint)
        :session_(session),audio_(audio),checkpoint_(std::move(checkpoint)) {
        aptHook(&cookie_,callback,this);
    }
    ~Suspension() {aptUnhook(&cookie_);}
    const std::string& error() const {return error_;}
private:
    static void callback(APT_HookType event,void* pointer) noexcept {
        auto& self=*static_cast<Suspension*>(pointer);
        try {
            if(event==APTHOOK_ONSUSPEND || event==APTHOOK_ONSLEEP || event==APTHOOK_ONEXIT) {
                self.audio_.pause(true);
                self.session_.advance(monotonic_time(),0,false);
                self.checkpoint_();
            } else if(event==APTHOOK_ONRESTORE || event==APTHOOK_ONWAKEUP) self.audio_.pause(false);
        } catch(const std::exception& error) {self.error_=error.what();}
    }
    ctr::GameSession& session_;ctr::NativeAudio& audio_;std::function<void()> checkpoint_;
    aptHookCookie cookie_{};std::string error_;
};
}

int main() {
    using namespace starfox;
    ctr::NativeDisplay display;
    ctr::Canvas top(ctr::top_width),lower;
    std::unique_ptr<ctr::NativeAudio> audio;
    std::unique_ptr<ctr::GameSession> session;
    std::unique_ptr<ctr::GameModels> models;
    std::unique_ptr<ctr::GameMenu> menu;
    std::unique_ptr<Suspension> suspension;
#if defined(STARFOX_3DS_CORE_PICA)
    std::unique_ptr<ctr::NativeGpu> gpu;
    std::unique_ptr<ctr::GameLayers> layers;
    ctr::PicaComposite composite;ctr::PicaColourEffects colour;ctr::PicaWindow window;
    // Explicitly label this experimental source-scene test. This small host
    // strip is not a replacement pre-game menu or part of source colour math.
    ctr::Canvas label_canvas(ctr::top_width);
    label_canvas.clear({8,15,28});label_canvas.text(4,4,"NATIVE PORT CHECK / FULL FLOW STILL PENDING",{240,181,86});
    constexpr std::array<ctr::Point3,4> label_corners{{{0,0,0},{400,0,0},{400,16,0},{0,16,0}}};
    constexpr std::array<std::array<float,2>,4> label_uv{{{0,0},{1,0},{1,1},{0,1}}};
    std::array<ctr::PicaVertex,6> label_vertices{};unsigned label_index=0;
    for(unsigned corner:{0U,1U,2U,0U,2U,3U}) label_vertices[label_index++]={label_corners[corner],{1,1,1,1},label_uv[corner]};
    ctr::PicaDraw label_draw{0,6,0,ctr::pica_identity,ctr::PicaSpace::screen,false,false,false};label_draw.source_layer=0;
    const ctr::PicaImage label_image{label_canvas.view().pixels.first(400*16*3),400,16,400*3,3};
#endif
    std::string error;
    auto experience=simulation::Experience::original;
    bool running=false;input::ButtonMask previous{};
    unsigned rasters{},logic{},blocks{};
    bool first_load=true;
    std::array<std::vector<std::uint8_t>,2> cartridge_ram;
    std::array<std::uint32_t,2> bank_crc{};
    ctr::GameStorage storage("sdmc:/3ds/starfox-enhanced",ctr::companion_manifest);
    ctr::GameSaveData saved;
    std::string save_warning;
    bool save_enabled=false;
    std::uint32_t cartridge_crc{};
    std::int64_t last_save_check{};
    try {
        const auto& loaded=storage.load();saved=loaded.data;experience=saved.experience;
        save_enabled=loaded.writable;save_warning=loaded.warning;
        cartridge_ram[unsigned(simulation::Experience::starfox_ex)]=saved.ex_sram;
        bank_crc[unsigned(simulation::Experience::starfox_ex)]=saved.ex_rom_crc;
    } catch(const std::exception& failure) {save_warning=failure.what();}
    const auto checkpoint=[&](bool force) {
        if(!session || !save_enabled) return;
        const auto now=monotonic_time();
        // No disk activity per eye/frame. Unchanged data does not write, and
        // ordinary checks are bounded to once per second. APT/exit/handoffs
        // checkpoint immediately before the source/SD owners are retired.
        if(!force && now-last_save_check<1'000'000'000LL) return;
        last_save_check=now;
        try {
            auto next=saved;next.experience=session->game().experience();
            next.preferences=session->preferences();
            if(session->game().in_setup_menu() && !session->game().runtime_options_open())
                next.preview=session->game().preview_requested();
            if(session->cartridge_experience()==simulation::Experience::starfox_ex) {
                const auto ram=session->cartridge_ram();
                next.ex_sram.assign(ram.begin(),ram.end());next.ex_rom_crc=cartridge_crc;
            }
            storage.save(next);saved=std::move(next);save_warning=storage.current().warning;
        } catch(const std::exception& failure) {
            save_warning=std::string(failure.what())+"\nSD saving disabled until restart. Existing valid slot preserved.";
            save_enabled=false;
        }
    };
    const auto release_owners=[&] {
        if(audio) audio->pause(true);
#if defined(STARFOX_3DS_CORE_PICA)
        gpu.reset();layers.reset();
#endif
        // All GPU views/asset references and the APT hook retire first.
        menu.reset();suspension.reset();models.reset();session.reset();audio.reset();
    };
    const auto load=[&](const char* map,ctr::GameSessionOptions options={}) {
        release_owners();
        top.clear({8,15,28});lower.clear({8,15,28});
        top.text(24,100,"RENDERING",{240,181,86},2);
        lower.text(12,16,options.preview?"PREPARING REAL CARTRIDGE PREVIEW":"LOADING CARTRIDGE AND SETTINGS",{183,224,240});
        display.present(ctr::plan_frame(0,false,ctr::ScreenUse::setup),top.view(),{},lower.view());
        std::ifstream file(companion_path,std::ios::binary);
        auto cartridge=ctr::read_game_cartridge(file,ctr::companion_manifest,experience);
        cartridge_crc=assets::crc32(cartridge.rom.bytes());
        auto& bank=cartridge_ram[unsigned(experience)];
        if(experience==simulation::Experience::starfox_ex && !bank.empty() && bank_crc[unsigned(experience)]!=cartridge_crc) {
            // Do not feed SRAM to a different cartridge or overwrite its save
            // with that cartridge's first-boot defaults.
            bank.clear();save_enabled=false;
            save_warning="EX save belongs to a different cartridge.\nSD saving disabled; back up journal files before recovery.";
        }
        audio=std::make_unique<ctr::NativeAudio>();
        options.preview_progress=[&](unsigned) {return display.poll().running;};
        session=std::make_unique<ctr::GameSession>(std::move(cartridge.rom),std::move(cartridge.symbols),
            [&](auto pcm){audio->submit(pcm);},map,cartridge_ram[unsigned(experience)],options);
        models=std::make_unique<ctr::GameModels>(session->rom(),session->symbols());
        menu=std::make_unique<ctr::GameMenu>(session->rom(),session->symbols());
        suspension=std::make_unique<Suspension>(*session,*audio,[&]{checkpoint(true);});
#if defined(STARFOX_3DS_CORE_PICA)
        layers=std::make_unique<ctr::GameLayers>();
        gpu=std::make_unique<ctr::NativeGpu>(ctr::pica_scene_shader);
#endif
        running=true;rasters=logic=blocks=0;error.clear();
        // The initiating physical A/Start belongs to loading, not the new menu.
        session->advance(monotonic_time(),0,false);
    };
    while(true) {
        const auto controls=display.poll();if(!controls.running) break;
        if((controls.held&(input::select|input::start))==(input::select|input::start)) break;
        const auto pressed=static_cast<input::ButtonMask>(controls.held&~previous);previous=controls.held;
        try {
            if(!running && (pressed&input::x)) experience=experience==simulation::Experience::original
                ?simulation::Experience::starfox_ex:simulation::Experience::original;
            if(first_load || (!running && (pressed&(input::a|input::y)))) {
                first_load=false;
                ctr::GameSessionOptions options;options.preferences=saved.preferences;
                options.preview=saved.preview && !(pressed&input::y);
                const auto initial_map=((pressed&input::y) || options.preview)?"LEVEL1_1":"BOOT";
                load(initial_map,options);
            }
            if(running) {
                if(!suspension->error().empty()) throw std::runtime_error(suspension->error());
                const auto advanced=session->advance(monotonic_time(),controls.held);
                rasters+=advanced.video_phases;logic+=advanced.logic_ticks;blocks+=advanced.audio_blocks;
                if(advanced.requested_settings_reset) {
                    checkpoint(true);
                    // Preserve the current real EX bank even if SD writing is
                    // disabled. Reset all settings by replacing, not partially
                    // mutating, the source owner. No game-save erasure.
                    const auto ram=session->cartridge_ram();
                    cartridge_ram[unsigned(session->cartridge_experience())].assign(ram.begin(),ram.end());
                    bank_crc[unsigned(session->cartridge_experience())]=cartridge_crc;
                    if(session->cartridge_experience()==simulation::Experience::starfox_ex) {
                        saved.ex_sram.assign(ram.begin(),ram.end());saved.ex_rom_crc=cartridge_crc;
                    }
                    saved=ctr::default_game_settings(std::move(saved));
                    if(save_enabled) try {storage.save(saved);save_warning=storage.current().warning;}
                    catch(const std::exception& failure) {
                        save_warning=std::string(failure.what())+"\nDefaults applied in memory; SD saving disabled until restart.";
                        save_enabled=false;
                    }
                    experience=simulation::Experience::original;
                    ctr::GameSessionOptions options;options.preferences=saved.preferences;
                    load("BOOT",options);continue;
                }
                if(advanced.requested_experience || advanced.requested_preview) {
                    checkpoint(true);
                    ctr::GameSessionOptions options;
                    options.preferences=session->preferences();
                    options.preview=advanced.requested_preview.value_or(session->game().menu_preview());
                    options.start_after_preview=advanced.start_after_preview;
                    const auto ram=session->cartridge_ram();
                    cartridge_ram[unsigned(session->cartridge_experience())].assign(ram.begin(),ram.end());
                    bank_crc[unsigned(session->cartridge_experience())]=cartridge_crc;
                    if(advanced.requested_experience) experience=*advanced.requested_experience;
                    load(options.preview?"LEVEL1_1":"BOOT",options);
                    continue;
                }
                checkpoint(false);
                const auto source=session->presentation(controls.slider,controls.stereoscopic_hardware,session->stereo_settings());
                auto dashboard=source.dashboard;
                if(!save_warning.empty() && session->game().in_setup_menu()) {
                    lower.clear({0,0,0});lower.image(0,0,dashboard);
                    lower.text(8,8,"SD SAVE WARNING\n"+save_warning,{239,90,99},1,304,216);
                    dashboard=lower.view();
                }
                if(session->settings_reset_hold().active()) {
                    if(dashboard.pixels.data()!=lower.view().pixels.data()) {
                        lower.clear({0,0,0});lower.image(0,0,dashboard);
                    }
                    const auto seconds=session->settings_reset_hold().elapsed()/1'000'000'000LL;
                    lower.rectangle(0,207,320,33,{8,15,28});
                    lower.text(8,210,"HOLD L+R: RESET SETTINGS "+std::to_string(seconds)+"/5\nRELEASE TO CANCEL / GAME SAVE KEPT",{240,181,86},1,304,28);
                    dashboard=lower.view();
                }
                if(advanced.logic_ticks || menu->state().visible!=session->game().in_setup_menu())
                    menu->update(ctr::GameMenu::capture(session->game()));
                const bool plain=menu->state().visible && !menu->state().preview;
#if defined(STARFOX_3DS_CORE_PICA)
                if(plain) {
                    // Preview OFF does not prepare models, decode BG layers,
                    // allocate scene textures, or submit either world eye.
                    gpu->present(menu->frame(source.plan),dashboard);continue;
                }
                const auto model_frame=models->prepare(source);
                const auto artwork=layers->prepare(source);
                const auto math=colour.prepare(source.raster->circle,source.raster->colour_math,
                    source.raster->brightness,source.plan);
                const bool world=source.current->flow==simulation::GameFlowState::gameplay
                    || source.current->flow==simulation::GameFlowState::training;
                const auto mask=window.prepare(source.raster->wipe,source.plan,
                    world?ctr::WindowCoverage::full_scene:ctr::WindowCoverage::authored);
                // Scene/raster/model clocks are shared by the eyes. Only PICA
                // eye matrices differ. The lower cockpit never joins a wipe.
                const ctr::PicaFrame label{source.plan,label_vertices,std::span(&label_draw,1),std::span(&label_image,1)};
                const auto frame=composite.prepare(source.plan,
                    std::array{artwork.before_models,model_frame,artwork.after_models,math,mask,label,menu->frame(source.plan)},dashboard,artwork.clear);
                gpu->present(frame,dashboard);
                continue; // Sole GPU owner: never also swap through NativeDisplay.
#else
                if(plain) {
                    display.present(source.plan,menu->plain_view(),{},dashboard);continue;
                }
                static_cast<void>(models->prepare(source));
#endif
                if(dashboard.pixels.data()!=lower.view().pixels.data()) {
                    lower.clear({0,0,0});lower.image(0,0,dashboard);
                }
            }
        } catch(const std::exception& failure) {
            error=failure.what();running=false;
            release_owners();
        }
        // Explicit diagnostic panel, not a fabricated/replacement pre-game
        // menu, nor a mono image pretending to be native stereoscopic gameplay.
        top.clear({8,15,28});
        top.text(12,12,"STAR FOX ENHANCED / ACTUAL SOURCE CORE",{183,224,240});
        top.text(12,38,"BRING-UP ONLY / FULL FLOW PENDING",{240,181,86});
        top.text(12,64,experience==simulation::Experience::original?"CARTRIDGE: ORIGINAL":"CARTRIDGE: EX",{227,235,242},2);
        if(running) {
            const auto coverage=models->coverage();
            top.text(12,100,"REAL BOOT VM + SPC + LOWER HUD RUNNING\nSOURCE RASTERS: "+std::to_string(rasters)
                +"\nLOGIC TICKS: "+std::to_string(logic)+"\nPCM BLOCKS: "+std::to_string(blocks)
                +"\nNATIVE MODELS: "+std::to_string(coverage.models),{227,235,242});
        } else {
            lower.clear({8,15,28});lower.text(12,12,"SOURCE CORE CHECK / NOT THE GAME",{183,224,240});
            top.text(12,100,"A: LOAD STANDARD COMPANION AND RUN BOOT\nY: DIRECT LEVEL1_1 SOURCE SCENE CHECK\nX: SELECT ORIGINAL / EX\nSELECT + START: EXIT\n\nSD CARD: /3ds/starfox-enhanced/\nStarfox-Assets.BIN",{227,235,242});
            if(!error.empty() || !save_warning.empty()) lower.text(12,40,"LOAD / CORE / SD ERROR\n"+error+"\n"+save_warning,{239,90,99},1,296,186);
        }
        display.present(ctr::plan_frame(0,false,ctr::ScreenUse::setup),top.view(),{},lower.view());
    }
    // Hook, model and session references retire before DSP storage / LCDs.
    checkpoint(true);
    release_owners();
}

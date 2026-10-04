// Actual VM/SPC/HUD link and SD bring-up, NOT the completed game renderer.
#include "native_display.hpp"
#include "native_audio.hpp"
#include "companion_manifest.hpp"
#include "starfox/platform/nintendo_3ds/game_assets.hpp"
#include "starfox/platform/nintendo_3ds/game_session.hpp"
#include "starfox/platform/nintendo_3ds/game_models.hpp"
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
    Suspension(ctr::GameSession& session,ctr::NativeAudio& audio):session_(session),audio_(audio) {
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
            } else if(event==APTHOOK_ONRESTORE || event==APTHOOK_ONWAKEUP) self.audio_.pause(false);
        } catch(const std::exception& error) {self.error_=error.what();}
    }
    ctr::GameSession& session_;ctr::NativeAudio& audio_;aptHookCookie cookie_{};std::string error_;
};
}

int main() {
    using namespace starfox;
    ctr::NativeDisplay display;
    ctr::Canvas top(ctr::top_width),lower;
    std::unique_ptr<ctr::NativeAudio> audio;
    std::unique_ptr<ctr::GameSession> session;
    std::unique_ptr<ctr::GameModels> models;
    std::unique_ptr<Suspension> suspension;
#if defined(STARFOX_3DS_CORE_PICA)
    std::unique_ptr<ctr::NativeGpu> gpu;
    std::unique_ptr<ctr::GameLayers> layers;
    ctr::PicaComposite composite;ctr::PicaColourEffects colour;ctr::PicaWindow window;
    // Explicitly label this experimental source-scene test. This small host
    // strip is not a replacement pre-game menu or part of source colour math.
    ctr::Canvas label_canvas(ctr::top_width);
    label_canvas.clear({8,15,28});label_canvas.text(4,4,"SOURCE SCENE CHECK / TERRAIN + MENU PENDING",{240,181,86});
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
    while(true) {
        const auto controls=display.poll();if(!controls.running) break;
        if((controls.held&(input::select|input::start))==(input::select|input::start)) break;
        const auto pressed=static_cast<input::ButtonMask>(controls.held&~previous);previous=controls.held;
        try {
            if(!running && (pressed&input::x)) experience=experience==simulation::Experience::original
                ?simulation::Experience::starfox_ex:simulation::Experience::original;
            if(!running && (pressed&(input::a|input::y))) {
                const auto initial_map=(pressed&input::y)?"LEVEL1_1":"BOOT";
                std::ifstream file(companion_path,std::ios::binary);
                auto cartridge=ctr::read_game_cartridge(file,ctr::companion_manifest,experience);
                // Owner destruction order is important: neither the model cache
                // nor an APT callback may outlive their immutable cartridge.
                suspension.reset();models.reset();session.reset();audio.reset();
#if defined(STARFOX_3DS_CORE_PICA)
                gpu.reset();layers.reset();
#endif
                audio=std::make_unique<ctr::NativeAudio>();
                session=std::make_unique<ctr::GameSession>(std::move(cartridge.rom),std::move(cartridge.symbols),
                    [&](auto pcm){audio->submit(pcm);},initial_map);
                models=std::make_unique<ctr::GameModels>(session->rom(),session->symbols());
                suspension=std::make_unique<Suspension>(*session,*audio);
#if defined(STARFOX_3DS_CORE_PICA)
                layers=std::make_unique<ctr::GameLayers>();
                gpu=std::make_unique<ctr::NativeGpu>(ctr::pica_scene_shader);
#endif
                running=true;rasters=logic=blocks=0;error.clear();
                // A started the bring-up, not the real BOOT menu. Suppress it
                // until released using the same suspend/resume input contract.
                session->advance(monotonic_time(),0,false);
            }
            if(running) {
                if(!suspension->error().empty()) throw std::runtime_error(suspension->error());
                const auto advanced=session->advance(monotonic_time(),controls.held);
                rasters+=advanced.video_phases;logic+=advanced.logic_ticks;blocks+=advanced.audio_blocks;
                if(advanced.requested_experience)
                    throw std::runtime_error("Cartridge handoff requested; native settings/renderer handoff still pending");
                const auto source=session->presentation(controls.slider,controls.stereoscopic_hardware);
                const auto model_frame=models->prepare(source);
#if defined(STARFOX_3DS_CORE_PICA)
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
                    std::array{artwork.before_models,model_frame,artwork.after_models,math,mask,label},source.dashboard,artwork.clear);
                gpu->present(frame,source.dashboard);
                continue; // Sole GPU owner: never also swap through NativeDisplay.
#else
                static_cast<void>(model_frame);
#endif
                lower.clear({0,0,0});lower.image(0,0,source.dashboard);
            }
        } catch(const std::exception& failure) {
            error=failure.what();running=false;
#if defined(STARFOX_3DS_CORE_PICA)
            gpu.reset();layers.reset();
#endif
            suspension.reset();models.reset();session.reset();audio.reset();
        }
        // Explicit diagnostic panel, not a fabricated/replacement pre-game
        // menu, nor a mono image pretending to be native stereoscopic gameplay.
        top.clear({8,15,28});
        top.text(12,12,"STAR FOX ENHANCED / ACTUAL SOURCE CORE",{183,224,240});
        top.text(12,38,"BRING-UP ONLY / TERRAIN + MENU PENDING",{240,181,86});
        top.text(12,64,experience==simulation::Experience::original?"CARTRIDGE: ORIGINAL":"CARTRIDGE: EX",{227,235,242},2);
        if(running) {
            const auto coverage=models->coverage();
            top.text(12,100,"REAL BOOT VM + SPC + LOWER HUD RUNNING\nSOURCE RASTERS: "+std::to_string(rasters)
                +"\nLOGIC TICKS: "+std::to_string(logic)+"\nPCM BLOCKS: "+std::to_string(blocks)
                +"\nNATIVE MODELS: "+std::to_string(coverage.models),{227,235,242});
        } else {
            lower.clear({8,15,28});lower.text(12,12,"SOURCE CORE CHECK / NOT THE GAME",{183,224,240});
            top.text(12,100,"A: LOAD STANDARD COMPANION AND RUN BOOT\nY: DIRECT LEVEL1_1 SOURCE SCENE CHECK\nX: SELECT ORIGINAL / EX\nSELECT + START: EXIT\n\nSD CARD: /3ds/starfox-enhanced/\nStarfox-Assets.BIN",{227,235,242});
            if(!error.empty()) lower.text(12,40,"LOAD / CORE ERROR\n"+error,{239,90,99},1,296,186);
        }
        display.present(ctr::plan_frame(0,false,ctr::ScreenUse::setup),top.view(),{},lower.view());
    }
    // Hook, model and session references retire before DSP storage / LCDs.
    suspension.reset();models.reset();session.reset();audio.reset();
#if defined(STARFOX_3DS_CORE_PICA)
    gpu.reset();layers.reset();
#endif
}

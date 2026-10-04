// Actual VM/SPC/HUD link and SD bring-up, NOT the completed game renderer.
#include "native_display.hpp"
#include "native_audio.hpp"
#include "companion_manifest.hpp"
#include "starfox/platform/nintendo_3ds/game_assets.hpp"
#include "starfox/platform/nintendo_3ds/game_session.hpp"
#include "starfox/platform/nintendo_3ds/game_models.hpp"
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
            if(!running && (pressed&input::a)) {
                std::ifstream file(companion_path,std::ios::binary);
                auto cartridge=ctr::read_game_cartridge(file,ctr::companion_manifest,experience);
                // Owner destruction order is important: neither the model cache
                // nor an APT callback may outlive their immutable cartridge.
                suspension.reset();models.reset();session.reset();audio.reset();
                audio=std::make_unique<ctr::NativeAudio>();
                session=std::make_unique<ctr::GameSession>(std::move(cartridge.rom),std::move(cartridge.symbols),
                    [&](auto pcm){audio->submit(pcm);});
                models=std::make_unique<ctr::GameModels>(session->rom(),session->symbols());
                suspension=std::make_unique<Suspension>(*session,*audio);
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
                static_cast<void>(models->prepare(source));
                lower.clear({0,0,0});lower.image(0,0,source.dashboard);
            }
        } catch(const std::exception& failure) {
            error=failure.what();running=false;
            suspension.reset();models.reset();session.reset();audio.reset();
        }
        // Explicit diagnostic panel, not a fabricated/replacement pre-game
        // menu, nor a mono image pretending to be native stereoscopic gameplay.
        top.clear({8,15,28});
        top.text(12,12,"STAR FOX ENHANCED / ACTUAL SOURCE CORE",{183,224,240});
        top.text(12,38,"BRING-UP ONLY / WORLD RENDERER PENDING",{240,181,86});
        top.text(12,64,experience==simulation::Experience::original?"CARTRIDGE: ORIGINAL":"CARTRIDGE: EX",{227,235,242},2);
        if(running) {
            const auto coverage=models->coverage();
            top.text(12,100,"REAL BOOT VM + SPC + LOWER HUD RUNNING\nSOURCE RASTERS: "+std::to_string(rasters)
                +"\nLOGIC TICKS: "+std::to_string(logic)+"\nPCM BLOCKS: "+std::to_string(blocks)
                +"\nNATIVE MODELS: "+std::to_string(coverage.models),{227,235,242});
        } else {
            lower.clear({8,15,28});lower.text(12,12,"SOURCE CORE CHECK / NOT THE GAME",{183,224,240});
            top.text(12,100,"A: LOAD STANDARD COMPANION AND RUN BOOT\nX: SELECT ORIGINAL / EX\nSELECT + START: EXIT\n\nSD CARD: /3ds/starfox-enhanced/\nStarfox-Assets.BIN",{227,235,242});
            if(!error.empty()) lower.text(12,40,"LOAD / CORE ERROR\n"+error,{239,90,99},1,296,186);
        }
        display.present(ctr::plan_frame(0,false,ctr::ScreenUse::setup),top.view(),{},lower.view());
    }
    // Hook, model and session references retire before DSP storage / LCDs.
    suspension.reset();models.reset();session.reset();audio.reset();
}

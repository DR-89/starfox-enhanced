// Asset-free LCD/input/projection diagnostic, NOT a playable Star Fox port.
#include "native_display.hpp"
#include "native_audio.hpp"
#include "starfox/platform/nintendo_3ds/pica_projection.hpp"

int main() {
    using namespace starfox::platform::nintendo_3ds;
    NativeDisplay display;
    std::array<Canvas,2> eyes{Canvas(top_width),Canvas(top_width)};CockpitDashboard lower;
    std::unique_ptr<NativeAudio> audio;
    std::array<std::int16_t,AudioPcm::samples> tone{};
    unsigned audio_block=20;
    std::string audio_message="X: TEST LEFT / RIGHT AUDIO";
    std::string audio_error;
    bool setup=true;starfox::input::ButtonMask previous{};
    while(true) {
        const auto input=display.poll();if(!input.running) break;
        if((input.held&(starfox::input::select|starfox::input::start))==(starfox::input::select|starfox::input::start)) break;
        if((input.held&starfox::input::a) && !(previous&starfox::input::a)) setup=false;
        if((input.held&starfox::input::b) && !(previous&starfox::input::b)) setup=true;
        if((input.held&starfox::input::x) && !(previous&starfox::input::x)) {
            try {
                if(!audio) audio=std::make_unique<NativeAudio>();
                // Do not rewrite a playing test on a second X press. A reset
                // explicitly retires the old DSP worker before reusing memory.
                else audio->reset();
                audio_block=0;audio_message="AUDIO TEST: LEFT THEN RIGHT";audio_error.clear();
            } catch(const std::exception& error) {
                audio.reset();audio_error=error.what();audio_message="AUDIO ERROR - SEE TOP LCD";
            }
        }
        if(audio) {
            try {
                // Keep only 100ms queued; pool capacity is not target latency.
                while(audio_block<20 && audio->available_blocks()>AudioPcm::blocks-2) {
                    tone.fill(0);
                    const unsigned side=audio_block<10?0:1;
                    const double frequency=side?440:220;
                    for(unsigned frame=0;frame<AudioPcm::frames;++frame) {
                        const auto sample=audio_block*AudioPcm::frames+frame;
                        tone[frame*2+side]=static_cast<std::int16_t>(4096*std::sin(
                            6.283185307179586*frequency*sample/AudioPcm::rate));
                    }
                    audio->submit(tone);++audio_block;
                }
                if(audio_block==20 && audio->available_blocks()==AudioPcm::blocks)
                    audio_message="AUDIO DONE / X: REPEAT";
            } catch(const std::exception& error) {
                audio.reset();audio_error=error.what();audio_message="AUDIO ERROR - SEE TOP LCD";
            }
        }
        previous=input.held;
        const auto plan=plan_frame(input.slider,input.stereoscopic_hardware,setup?ScreenUse::setup:ScreenUse::world);
        for(unsigned eye=0;eye<plan.eye_count;++eye) {
            eyes[eye].clear({8,15,28});
            eyes[eye].text(18,12,"STAR FOX ENHANCED / 3DS FRONTEND CHECK",{183,224,240});
            if(setup) {
                eyes[eye].text(32,66,"DIAGNOSTIC ONLY - NO GAME ASSETS\n\nA: CHECK SLIDER DEPTH\nB: RETURN TO THIS SCREEN\nX: LEFT / RIGHT AUDIO TEST\nSELECT + START: EXIT\n\nREAL PRE-GAME MENU PENDING",{227,235,242},2);
            } else {
                const PicaProjection projection(plan,eye);
                for(float depth:{256.F,512.F,1024.F,3072.F}) {
                    const std::array<Point3,4> corners{{{-64,48,depth},{64,48,depth},{64,-48,depth},{-64,-48,depth}}};
                    for(unsigned edge=0;edge<4;++edge) {
                        if(const auto line=projection.project_segment(corners[edge],corners[(edge+1)%4]))
                            eyes[eye].line(int((*line)[0][0]),int((*line)[0][1]),int((*line)[1][0]),int((*line)[1][1]),
                                depth<1024?Rgb{94,204,229}:Rgb{240,181,86});
                    }
                }
                eyes[eye].text(28,220,"SLIDER: DEPTH / B: BACK / SELECT+START: EXIT",{213,237,244});
            }
            if(!audio_error.empty()) {
                eyes[eye].rectangle(16,50,368,166,{17,25,38});
                eyes[eye].text(24,58,"AUDIO OUTPUT ERROR",{239,90,99},2);
                eyes[eye].text(24,82,audio_error,{227,235,242},1,352,108);
                eyes[eye].text(24,202,"X: RETRY / SELECT + START: EXIT",{183,224,240});
            }
        }
        HudState hud;hud.lives=2;hud.bombs=3;hud.shield_percent=76;hud.boost_percent=92;
        hud.ally_percent={84,58,95};hud.radio_message=audio_message;
        lower.update(hud);
        display.present(plan,eyes[0].view(),plan.stereo?eyes[1].view():ImageView{},lower.view());
    }
}

#include "native_display.hpp"
#include <3ds.h>

namespace starfox::platform::nintendo_3ds {
NativeDisplay::NativeDisplay() {
    // Keep the baseline honest on New hardware too; the primary target is
    // original 3DS/XL, not a port that secretly requires New 3DS clocks.
    osSetSpeedupEnable(false);
    if(R_SUCCEEDED(cfguInit())) {
        u8 model{};
        if(R_SUCCEEDED(CFGU_GetSystemModel(&model)))
            stereoscopic_hardware_=model==CFG_MODEL_3DS || model==CFG_MODEL_3DSXL
                || model==CFG_MODEL_N3DS || model==CFG_MODEL_N3DSXL;
        cfguExit();
    }
    gfxInitDefault();gfxSet3D(false);
}
NativeDisplay::~NativeDisplay() {gfxExit();}
NativeInput NativeDisplay::poll() {
    const bool running=aptMainLoop();
    if(!running) return {};
    hidScanInput();circlePosition circle{};hidCircleRead(&circle);
    const auto physical=hidKeysHeld();
    return {buttons(physical,circle.dx,circle.dy),osGet3DSliderState(),true,stereoscopic_hardware_,
        buttons(physical),circle.dx,circle.dy};
}
void NativeDisplay::present(const FramePlan& frame,ImageView left,ImageView right,ImageView lower) {
    if(!valid_image(left,top_width,screen_height) || !valid_image(lower,bottom_width,screen_height)
        || (frame.stereo && (!valid_image(right,top_width,screen_height)
            || right.pixels.data()==left.pixels.data())) || frame.eye_count!=(frame.stereo?2U:1U))
        throw std::invalid_argument("Incomplete independently rendered 3DS frame");
    gfxSet3D(frame.stereo);
    auto* top_left=gfxGetFramebuffer(GFX_TOP,GFX_LEFT,nullptr,nullptr);
    auto* bottom=gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,nullptr,nullptr);
    auto* top_right=frame.stereo?gfxGetFramebuffer(GFX_TOP,GFX_RIGHT,nullptr,nullptr):nullptr;
    if(!top_left || !bottom || (frame.stereo && !top_right)) throw std::runtime_error("3DS LCD allocation failed");
    copy_lcd(left,{top_left,top_width*screen_height*3});
    if(frame.stereo) copy_lcd(right,{top_right,top_width*screen_height*3});
    copy_lcd(lower,{bottom,bottom_width*screen_height*3});
    gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();
}
} // namespace starfox::platform::nintendo_3ds

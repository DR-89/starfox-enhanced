#pragma once
#include "starfox/platform/nintendo_3ds/frontend.hpp"

namespace starfox::platform::nintendo_3ds {
struct NativeInput {
    input::ButtonMask held{};float slider{};bool running{},stereoscopic_hardware{};
    input::ButtonMask physical{};int circle_x{},circle_y{};
};
// LCD/input adapter only. The game renderer must supply independent projected
// eyes. This does not pretend that SDL's software presenter is a PICA renderer.
class NativeDisplay {
public:
    NativeDisplay();~NativeDisplay();
    NativeDisplay(const NativeDisplay&)=delete;
    NativeDisplay& operator=(const NativeDisplay&)=delete;
    NativeInput poll();
    void present(const FramePlan&,ImageView left,ImageView right,ImageView lower);
private:
    bool stereoscopic_hardware_{};
};
} // namespace starfox::platform::nintendo_3ds

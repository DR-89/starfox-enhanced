#include "starfox/platform/nintendo_3ds/frame_profile.hpp"
#include <limits>
#include <sstream>
#include <stdexcept>
#include <iostream>

namespace {
std::uint64_t tick{};
unsigned clock_calls{};
std::uint64_t clock_tick() noexcept { ++clock_calls; return tick; }
void require(bool condition, const char* message) {
    if(!condition) throw std::runtime_error(message);
}
}
int main() try {
    using namespace starfox::platform::nintendo_3ds;
    FrameProfile profile(clock_tick, 1000);
    {
        ScopedFrameProfileActivation active(profile);
        { ScopedFramePhase parent(active_frame_profile, FramePhase::advance);
          tick = 20;
          { ScopedFramePhase child(active_frame_profile, FramePhase::audio); tick = 70; }
          tick = 90; }
        require(profile.totals()[unsigned(FramePhase::advance)] == FramePhaseTotals{1,90,90}, "Parent phase changed");
        require(profile.totals()[unsigned(FramePhase::audio)] == FramePhaseTotals{1,50,50}, "Child phase changed");
        FrameProfile nested(clock_tick, 1000);
        { ScopedFrameProfileActivation activation(nested); require(active_frame_profile == &nested, "Nested activation failed"); }
        require(active_frame_profile == &profile, "Nested activation lost previous owner");
        try { ScopedFramePhase failed(&profile, FramePhase::models); tick = 120; throw std::runtime_error("fixture"); }
        catch(const std::runtime_error&) {}
        require(profile.totals()[unsigned(FramePhase::models)] == FramePhaseTotals{1,30,30}, "Unwinding lost timing");
    }
    require(!active_frame_profile, "Profiler outlived owner");
    profile.record(FramePhase::audio, 200, 210);
    std::ostringstream stream;
    require(!profile.write_window(stream, 999, 9, 4, 30, 10, 10) && stream.str().empty(), "Sub-second diagnostic wrote data");
    require(profile.write_window(stream, 1000, 9, 4, 30, 10, 10), "Due diagnostic failed");
    require(stream.str() ==
        "tick,clock_hz,flow,bg,video_phases,logic_ticks,audio_blocks,phase,calls,total_ticks,max_ticks\n"
        "1000,1000,9,4,30,10,10,advance,1,90,90\n"
        "1000,1000,9,4,30,10,10,audio,2,60,50\n"
        "1000,1000,9,4,30,10,10,models,1,30,30\n", "CSV values, units or counts changed");
    require(profile.totals() == std::array<FramePhaseTotals,unsigned(FramePhase::count)>{}, "Accepted window did not reset");
    profile.record(FramePhase::dots, std::numeric_limits<std::uint64_t>::max()-5, 4);
    require(profile.totals()[unsigned(FramePhase::dots)] == FramePhaseTotals{1,10,10}, "Clock rollover lost duration");
    std::ostringstream broken; broken.setstate(std::ios::badbit);
    require(!profile.write_window(broken, 2000, 9, 4, 60, 20, 20) && profile.stopped(), "Bad stream not disabled");
    const auto stopped = profile.totals(); profile.record(FramePhase::logic, 0, 42);
    require(profile.totals() == stopped && !profile.write_window(stream, 3000, 9, 4, 60, 20, 20), "Failed diagnostic kept growing");
    tick = 0; FrameProfile capped(clock_tick, 1); std::ostringstream bounded;
    for(unsigned i = 1; i <= FrameProfile::maximum_windows; ++i) {
        capped.record(FramePhase::frame, 0, 1);
        require(capped.write_window(bounded, i, 0, 0, i, i, i), "Bounded window failed early");
    }
    const auto size = bounded.str().size();
    require(capped.stopped() && !capped.write_window(bounded, 999, 0, 0, 0, 0, 0) && bounded.str().size() == size,
        "Diagnostic window cap lost");
    FrameProfile no_clock(nullptr, 1), no_frequency(clock_tick, 0);
    for(auto* disabled : {&no_clock, &no_frequency}) {
        disabled->record(FramePhase::frame, 0, 100);
        require(!disabled->write_window(stream, 1000, 0, 0, 0, 0, 0)
            && disabled->totals() == std::array<FramePhaseTotals,unsigned(FramePhase::count)>{}, "Disabled clock emitted data");
    }
    FrameProfile macro_profile(clock_tick, 1);
    {
        ScopedFrameProfileActivation activation(macro_profile);
        const auto before = clock_calls;
        { STARFOX_3DS_FRAME_PHASE(video); tick += 7; }
#if defined(STARFOX_3DS_PROFILE_FRAMES)
        require(clock_calls == before+2 && macro_profile.totals()[unsigned(FramePhase::video)] == FramePhaseTotals{1,7,7},
            "Enabled native macro lost timing");
#else
        require(clock_calls == before && macro_profile.totals() == std::array<FramePhaseTotals,unsigned(FramePhase::count)>{},
            "Disabled native macro read the clock or changed stats");
#endif
    }
    FrameProfile throwing(clock_tick, 1); std::ostringstream failed_stream;
    failed_stream.setstate(std::ios::badbit);
    try { failed_stream.exceptions(std::ios::badbit); } catch(const std::ios_base::failure&) {}
    require(!throwing.write_window(failed_stream, tick+1, 0, 0, 0, 0, 0) && throwing.stopped(),
        "Throwing stream escaped noexcept diagnostic");
    std::cout << "3DS native phase totals, nested owners, rollover, unwinding, CSV cadence and failed/bounded logging pass\n";
} catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }

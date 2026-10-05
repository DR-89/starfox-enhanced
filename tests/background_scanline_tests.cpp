#include "starfox/render/background_renderer.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
using namespace starfox::render;
using starfox::simulation::SnesPpuState;

std::uint32_t random_word(std::uint32_t& seed) {
    seed ^= seed << 13U; seed ^= seed >> 17U; seed ^= seed << 5U;
    return seed;
}
void word(SnesPpuState& ppu, unsigned address, std::uint16_t value) {
    const unsigned byte = (address & 32767U) * 2U;
    ppu.vram[byte] = std::uint8_t(value);
    ppu.vram[byte + 1U] = std::uint8_t(value >> 8U);
}
SnesPpuState source(unsigned n) {
    SnesPpuState ppu;
    std::uint32_t seed = 0x6842de19U + n * 0x9e3779b9U;
    for (auto& byte : ppu.vram) byte = std::uint8_t(random_word(seed));
    for (unsigned i = 0; i < ppu.cgram.size(); ++i)
        ppu.cgram[i] = i % 13U == 0 ? 0 : std::uint16_t(random_word(seed) & 32767U);
    // Transparent ink, opaque CGRAM black, all palettes/priorities/flips and
    // sub-character carry, including maps and bitplanes wrapping at 64 KiB.
    ppu.main_screen = n % 29U == 0 ? 0 : 7;
    ppu.background_mode = std::uint8_t(1U + n % 3U);
    ppu.bg1_character_base = n % 5U ? 0x1000 : 0x7ffe;
    ppu.bg2_character_base = n % 7U ? 0x1800 : 0x7ff9;
    ppu.bg3_character_base = n % 11U ? 0x2800 : 0x7ffd;
    ppu.bg1_screen_base = n % 9U ? 0x6000 : 0x7ffb;
    ppu.bg2_screen_base = n % 13U ? 0x6800 : 0x7ffa;
    ppu.bg3_screen_base = n % 17U ? 0x7000 : 0x7ffc;
    ppu.bg1_screen_size = std::uint8_t(n & 3U);
    ppu.bg2_screen_size = std::uint8_t((n >> 2U) & 3U);
    ppu.bg3_screen_size = std::uint8_t((n >> 4U) & 3U);
    ppu.bg1_tile_size_16 = (n & 4U) != 0;
    ppu.bg2_tile_size_16 = (n & 16U) != 0;
    ppu.bg3_tile_size_16 = (n & 64U) != 0;
    ppu.bg1_scroll_x = std::int16_t(random_word(seed));
    ppu.bg1_scroll_y = std::int16_t(random_word(seed));
    ppu.bg2_scroll_x = std::int16_t(random_word(seed));
    ppu.bg2_scroll_y = std::int16_t(random_word(seed));
    ppu.bg3_scroll_x = std::int16_t(random_word(seed));
    ppu.bg3_scroll_y = std::int16_t(random_word(seed));
    ppu.mosaic = n % 4U ? 0 : std::uint8_t(((n % 16U) << 4U) | 7U);
    ppu.tunnel_scene = n % 6U == 0;
    ppu.bg2_horizontal_offsets_enabled = n % 3U == 1;
    ppu.bg2_scanline_scroll_enabled = n % 3U != 0;
    ppu.bg2_vertical_offsets_enabled = n % 5U != 0;
    for (unsigned y = 0; y < 224; ++y) {
        ppu.bg2_horizontal_offsets[y] = std::int16_t(int(y) / 7 - 31);
        ppu.bg2_scanline_scroll_y[y] = std::int16_t(y < 112U ? 137 : -97);
    }
    for (unsigned x = 0; x < 32; ++x) {
        const bool valid = n % 4U == 1 || (n % 4U == 2 && x % 3U != 0);
        word(ppu, 0x2fa0U + x, std::uint16_t((valid ? 0x4000U : 0)
            | (unsigned(int(x) * 7 - 119) & 8191U)));
    }
    return ppu;
}
void add(std::uint64_t& hash, std::span<const std::uint8_t> bytes) {
    for (const auto byte : bytes) { hash ^= byte; hash *= 1099511628211ULL; }
}
void number(std::uint64_t& hash, std::uint32_t value) {
    const std::array bytes{std::uint8_t(value), std::uint8_t(value >> 8U),
        std::uint8_t(value >> 16U), std::uint8_t(value >> 24U)};
    add(hash, bytes);
}
void draw(const SnesPpuState& ppu, Framebuffer& frame, unsigned n) {
    const BackgroundRenderer renderer;
    const auto priority = static_cast<TilePriorityPass>(n % 3U);
    const int origin = int(frame.width()) / 2 - 128 + int(n % 9U) - 4;
    const bool extend = n % 4U != 1;
    const BackgroundUniqueRegion region{48, 8, 208, 192, 1, 127, 4,
        int(128 | (n % 2U ? BackgroundUniqueRegion::suppress_every_copy
            : BackgroundUniqueRegion::suppress_all_side_copies))};
    {
        const ScopedLayer tag(frame, PixelLayer::background);
        renderer.draw_bg2(ppu, ppu.bg2_scroll_x, ppu.bg2_scroll_y, frame,
            priority, origin, extend, n % 5U != 0, n % 3U == 0,
            n % 7U == 0 ? 112U : 0U,
            n % 8U == 0 ? std::span(&region, 1) : std::span<const BackgroundUniqueRegion>{},
            n % 19U == 0, n % 23U == 0, n % 11U == 0 ? 72U : 0U);
    }
    {
        const ScopedLayer tag(frame, PixelLayer::two_d);
        renderer.draw_bg1(ppu, frame, priority, origin, extend, n % 19U,
            n % 4U == 0, n % 9U == 0, n % 13U == 0);
    }
    renderer.draw_bg3(ppu, frame, priority, origin, extend);
}
std::uint64_t oracle_trace() {
    std::uint64_t hash = 14695981039346656037ULL;
    constexpr std::array widths{0U, 1U, 127U, 256U, 400U, 853U};
    constexpr std::array heights{1U, 97U, 192U, 224U};
    for (unsigned n = 0; n < 192; ++n) {
        auto ppu = source(n);
        const unsigned scale = n % 17U == 0 ? 2U : 1U;
        Framebuffer frame(widths[n % widths.size()], heights[(n / 6U) % heights.size()], scale);
        frame.enable_layer_tags(true); frame.enable_dither_pairs(true);
        frame.clear(37); frame.begin_write_coverage();
        for (unsigned i = 0; i < frame.pixels().size(); ++i)
            frame.set_dither_alternate(i, std::uint8_t(i % 251U));
        draw(ppu, frame, n);
        // The old point-writer trace below is pinned independently. Native
        // bulk writes must reproduce that same covered/tagged source image.
        Framebuffer native(widths[n % widths.size()], heights[(n / 6U) % heights.size()], scale);
        native.enable_layer_tags(true); native.clear(37); native.begin_write_coverage();
        draw(ppu, native, n);
        if (native.pixels() != frame.pixels() || native.layer_tags() != frame.layer_tags()
            || !std::equal(native.write_coverage().begin(), native.write_coverage().end(),
                frame.write_coverage().begin()))
            throw std::runtime_error("native bulk tile-row writes differ from the pinned point-writer image");
        number(hash, n); add(hash, frame.pixels()); add(hash, frame.layer_tags());
        add(hash, frame.write_coverage());
        for (auto pair : frame.dither_pairs()) number(hash, pair);
    }
    // Command recording must retain its exact ordered point writes, rather
    // than silently replacing the GPU's source-raster interface with a span.
    for (unsigned n = 0; n < 12; ++n) {
        auto ppu = source(n + 9U);
        Framebuffer frame(33, 29, n % 2U + 1U);
        RasterCommands commands; commands.reset(frame.stored_width(), frame.stored_height());
        frame.record_to(&commands); draw(ppu, frame, n + 9U);
        number(hash, std::uint32_t(commands.commands.size()));
        for (const auto& command : commands.commands) {
            number(hash, unsigned(command.left)); number(hash, unsigned(command.top));
            number(hash, unsigned(command.right)); number(hash, unsigned(command.bottom));
            number(hash, command.even); number(hash, command.odd); number(hash, command.tag);
        }
        add(hash, frame.pixels());
    }
    return hash;
}
void benchmark() {
    using Clock = std::chrono::steady_clock;
    const BackgroundRenderer renderer;
    auto ppu = source(2);
    ppu.main_screen = 7; ppu.mosaic = 0; ppu.background_mode = 2;
    ppu.bg1_tile_size_16 = ppu.bg2_tile_size_16 = ppu.bg3_tile_size_16 = false;
    ppu.bg2_vertical_offsets_enabled = false; ppu.bg2_scanline_scroll_enabled = false;
    ppu.bg2_horizontal_offsets_enabled = false; ppu.tunnel_scene = false;
    Framebuffer frame(400, 224); frame.enable_layer_tags(true); frame.begin_write_coverage();
    std::uint64_t hash = 14695981039346656037ULL;
    std::cout << "layer,draws,milliseconds,pixel_digest\n";
    for (unsigned layer = 1; layer <= 4; ++layer) {
        ppu.bg2_vertical_offsets_enabled = layer == 4;
        constexpr unsigned draws = 200;
        const auto start = Clock::now();
        for (unsigned tick = 0; tick < draws; ++tick) {
            ppu.bg1_scroll_x = ppu.bg2_scroll_x = ppu.bg3_scroll_x = std::int16_t(tick);
            if (layer == 1) renderer.draw_bg1(ppu, frame);
            else if (layer == 3) renderer.draw_bg3(ppu, frame);
            else renderer.draw_bg2(ppu, ppu.bg2_scroll_x, ppu.bg2_scroll_y, frame);
        }
        const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        add(hash, frame.pixels()); add(hash, frame.write_coverage()); add(hash, frame.layer_tags());
        std::cout << layer << ',' << draws << ',' << elapsed << ',' << std::hex << hash << std::dec << '\n';
    }
}
} // namespace

int main(int argc, char** argv) try {
    if (argc == 2 && std::string_view(argv[1]) == "--benchmark") { benchmark(); return 0; }
    const auto digest = oracle_trace();
    // Filled from a separate executable built BEFORE changing the renderer:
    // background_renderer.cpp SHA256 450af058b47c38fd096b7416782f1b3ab
    // dcdd428b847388391a68958e7d587d0, GNU 13.2 / -O2 / c++20.
    // This is not a comparison between two wrappers of the optimized routine.
#ifndef STARFOX_BACKGROUND_LEGACY_ORACLE
    constexpr std::uint64_t expected = 0xe6f02e2ec125cb39ULL;
    if (digest != expected) throw std::runtime_error("background scanline pixels/coverage/tags/commands changed");
#endif
    std::cout << "Background source trace: 192 varied rasters + 12 ordered command streams, digest "
        << std::hex << digest << " PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
}

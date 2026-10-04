#include "starfox/render/framebuffer.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
std::uint64_t checks{};
void inspect(const starfox::render::Framebuffer& framebuffer) {
    // Independent pre-optimization formula, including arbitrary repartitions
    // of the same storage. Do not use the fast branch in the oracle.
    ++checks;
    if (framebuffer.draw_scale() == 0U
        || framebuffer.width() != framebuffer.stored_width() / framebuffer.draw_scale()
        || framebuffer.height() != framebuffer.stored_height() / framebuffer.draw_scale()
        || framebuffer.pixels().size()
            != std::size_t(framebuffer.stored_width()) * framebuffer.stored_height())
        throw std::runtime_error{"framebuffer logical extents changed"};
}
} // namespace

int main() try {
    constexpr std::array<std::uint32_t, 8> sizes{0, 1, 2, 7, 8, 17, 63, 129};
    constexpr std::array<std::uint32_t, 10> partitions{
        0, 1, 2, 3, 4, 5, 6, 16, 65536,
        std::numeric_limits<std::uint32_t>::max()};
    for (const auto width : sizes) for (const auto height : sizes)
        for (std::uint32_t initial = 0; initial <= 6; ++initial) {
            starfox::render::Framebuffer frame{width, height, initial};
            inspect(frame);
            frame.clear(37);
            for (const auto scale : partitions) {
                frame.set_draw_scale(scale);
                inspect(frame);
                const auto copy = frame;
                inspect(copy);
                for (const auto pixel : frame.pixels()) {
                    ++checks;
                    if (pixel != 37) throw std::runtime_error{"extent query altered stored pixels"};
                }
            }
            // Resume native writes, then alternate same-size and changed-size
            // resize paths under both native and noninteger source partitions.
            for (std::uint32_t scale = 0; scale <= 6; ++scale) {
                frame.set_draw_scale(scale);
                frame.resize(width, height);
                inspect(frame);
                frame.resize(width, height);
                inspect(frame);
                frame.resize(height, width);
                inspect(frame);
            }
        }
    std::cout << "Framebuffer extents: " << checks
        << " native/scaled/repartitioned/copy/resize comparisons PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}

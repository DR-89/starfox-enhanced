// Adapted from Esteban PDN / starwing-3ds, 753952be5b170059d95068350d37759a966a3bd5.
// Source: platform/3ds/source/bg2_plan.cpp. See UPSTREAM-STARWING.md.
#include "starfox/platform/nintendo_3ds/bg2_tile_plan.hpp"
#include <algorithm>

namespace starfox::platform::nintendo_3ds {
namespace {
std::uint16_t word(const simulation::SnesPpuState& ppu,unsigned address) {
    const unsigned at=(address&0x7fffU)*2;
    return std::uint16_t(ppu.vram[at]|(unsigned(ppu.vram[at+1])<<8));
}
}
bool plan_bg2_tiles(const simulation::SnesPpuState& ppu,int scroll_x,int scroll_y,
    unsigned width,int origin,int priority,std::vector<Bg2TileRect>& output,unsigned capacity) {
    output.clear();
    if(ppu.background_mode!=2 || (ppu.mosaic&2) || ppu.bg2_vertical_offsets_enabled
        || ppu.tunnel_scene || !width || width>4096 || origin<0 || origin>4096
        || priority< -1 || priority>1 || scroll_x< -32768 || scroll_x>32767
        || scroll_y< -32768 || scroll_y>32767 || capacity>16384) return false;
    if(!(ppu.main_screen&2)) return true;
    const unsigned columns=(ppu.bg2_screen_size&1)?64:32;
    const unsigned rows=(ppu.bg2_screen_size&2)?64:32;
    const unsigned edge=ppu.bg2_tile_size_16?16:8,shift=ppu.bg2_tile_size_16?4:3;
    const unsigned x_mask=columns*edge-1,y_mask=rows*edge-1,pages_wide=columns/32;
    const auto row_x=[&](unsigned y){return ppu.bg2_horizontal_offsets_enabled
        ?int(ppu.bg2_horizontal_offsets[y]):scroll_x;};
    const auto row_y=[&](unsigned y){return ppu.bg2_scanline_scroll_enabled
        ?int(ppu.bg2_scanline_scroll_y[y]):scroll_y;};
    std::vector<Bg2TileRect> next;next.reserve(std::min(capacity,2048U));
    for(unsigned band_top=0;band_top<224;) {
        const int band_x=row_x(band_top),band_y=row_y(band_top);
        unsigned band_end=band_top+1;
        while(band_end<224 && row_x(band_end)==band_x && row_y(band_end)==band_y) ++band_end;
        for(unsigned x=0;x<width;) {
            const unsigned sx=unsigned(int(x)-origin+band_x)&x_mask;
            const unsigned span=std::min(width-x,8-(sx&7));
            for(unsigned y=band_top;y<band_end;) {
                const unsigned sy=unsigned(int(y)+band_y)&y_mask;
                const unsigned height=std::min(band_end-y,8-(sy&7));
                const unsigned tx=sx>>shift,ty=sy>>shift;
                const unsigned entry=((tx>>5)+(ty>>5)*pages_wide)*1024+(ty&31)*32+(tx&31);
                const auto tile=word(ppu,unsigned(ppu.bg2_screen_base)+entry);
                if(priority<0 || bool(tile&0x2000)==bool(priority)) {
                    if(next.size()==capacity) return false;
                    const bool flip_x=(tile&0x4000)!=0,flip_y=(tile&0x8000)!=0;
                    unsigned character=tile&1023;
                    if(ppu.bg2_tile_size_16) {
                        const unsigned sub_x=flip_x?15-(sx&15):(sx&15);
                        const unsigned sub_y=flip_y?15-(sy&15):(sy&15);
                        character=(character+(sub_x>=8?1:0)+(sub_y>=8?16:0))&1023;
                    }
                    next.push_back({x,y,span,height,std::uint16_t(character),std::uint8_t((tile>>10)&7),
                        std::uint8_t(flip_x?7-(sx&7):(sx&7)),std::uint8_t(flip_y?7-(sy&7):(sy&7)),flip_x,flip_y});
                }
                y+=height;
            }
            x+=span;
        }
        band_top=band_end;
    }
    output.swap(next);return true;
}
} // namespace starfox::platform::nintendo_3ds

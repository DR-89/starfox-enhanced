#include "starfox/platform/nintendo_3ds/pica_raster.hpp"

namespace starfox::platform::nintendo_3ds {
namespace {
constexpr unsigned guard=32,native_height=224;
unsigned darkest(const std::array<std::uint16_t,256>& palette) {
    unsigned selected=0,luma=~0U;
    for(unsigned i=0;i<palette.size();++i) {
        const auto word=palette[i];const auto value=77U*(word&31)+150U*((word>>5)&31)+29U*((word>>10)&31);
        if(value<luma) {selected=i;luma=value;}
    }
    return selected;
}
bool same_source(const simulation::SnesPpuState& a,const simulation::SnesPpuState& b,const PpuPass& pass) {
    // Palette-only fades recolour cached indices. OAM activity must not force
    // another background tile traversal, nor scrolling a sky another OBJ pass.
    if(a.vram!=b.vram || (pass.transparent_black && a.cgram!=b.cgram)) return false;
    if(pass.layer==PpuLayer::objects)
        return a.oam==b.oam && a.object_select==b.object_select && (a.main_screen&16)==(b.main_screen&16);
    if(a.background_mode!=b.background_mode || a.mosaic!=b.mosaic) return false;
    switch(pass.layer) {
    case PpuLayer::bg1:
        return (a.main_screen&1)==(b.main_screen&1) && a.bg1_character_base==b.bg1_character_base
            && a.bg1_screen_base==b.bg1_screen_base && a.bg1_screen_size==b.bg1_screen_size
            && a.bg1_tile_size_16==b.bg1_tile_size_16 && a.bg1_scroll_x==b.bg1_scroll_x && a.bg1_scroll_y==b.bg1_scroll_y;
    case PpuLayer::bg2:
        return (a.main_screen&2)==(b.main_screen&2) && a.bg2_character_base==b.bg2_character_base
            && a.bg2_screen_base==b.bg2_screen_base && a.bg2_screen_size==b.bg2_screen_size
            && a.bg2_tile_size_16==b.bg2_tile_size_16 && a.bg2_scroll_x==b.bg2_scroll_x && a.bg2_scroll_y==b.bg2_scroll_y
            && a.bg2_horizontal_offsets_enabled==b.bg2_horizontal_offsets_enabled && a.bg2_horizontal_offsets==b.bg2_horizontal_offsets
            && a.bg2_vertical_offsets_enabled==b.bg2_vertical_offsets_enabled && a.bg2_scanline_scroll_enabled==b.bg2_scanline_scroll_enabled
            && a.bg2_scanline_scroll_y==b.bg2_scanline_scroll_y && a.tunnel_scene==b.tunnel_scene
            && (a.cgram==b.cgram || darkest(a.cgram)==darkest(b.cgram));
    case PpuLayer::bg3:
        return (a.main_screen&4)==(b.main_screen&4) && a.bg3_character_base==b.bg3_character_base
            && a.bg3_screen_base==b.bg3_screen_base && a.bg3_screen_size==b.bg3_screen_size
            && a.bg3_tile_size_16==b.bg3_tile_size_16 && a.bg3_scroll_x==b.bg3_scroll_x && a.bg3_scroll_y==b.bg3_scroll_y;
    default: return false;
    }
}
std::array<std::uint8_t,3> colour(std::uint16_t word,unsigned brightness,unsigned subtract) {
    std::array<std::uint8_t,3> result{};
    for(unsigned channel=0;channel<3;++channel) {
        const auto five=std::max(0,int((word>>(channel*5))&31)-int(subtract));
        result[channel]=std::uint8_t(((five<<3)|(five>>2))*brightness/15);
    }
    return result;
}
void validate(const simulation::SnesPpuState& ppu,const PpuBatch& batch,const FramePlan& plan,unsigned brightness,unsigned subtract) {
    if(ppu.background_mode<1 || ppu.background_mode>3 || brightness>15 || subtract>31
        || batch.passes.size()>16 || batch.first_row>=batch.last_row || batch.last_row>native_height
        || (batch.space!=PicaSpace::screen && batch.space!=PicaSpace::scenery)
        || (batch.space==PicaSpace::scenery && !batch.expand_horizontal))
        throw std::invalid_argument("Unsupported/incomplete 3DS PPU painter group");
    if(plan.eye_count!=(plan.stereo?2U:1U)) throw std::invalid_argument("Invalid 3DS PPU eye plan");
    for(unsigned eye=0;eye<plan.eye_count;++eye) {
        static_cast<void>(PicaProjection(plan,eye));
        if(batch.space==PicaSpace::scenery && std::abs(background_offset(plan,eye))>guard)
            throw std::invalid_argument("3DS infinite scenery exceeds its native guard coverage");
    }
    for(const auto& pass:batch.passes) {
        if((pass.layer!=PpuLayer::bg1 && pass.layer!=PpuLayer::bg2 && pass.layer!=PpuLayer::bg3 && pass.layer!=PpuLayer::objects)
            || pass.priority< -1 || pass.priority>(pass.layer==PpuLayer::objects?3:1)
            || pass.guard_inset>128 || pass.single_occurrence_top_rows>512
            || (batch.space==PicaSpace::scenery && pass.layer==PpuLayer::objects)
            || (pass.sprites!=render::SpriteSelection::all && pass.sprites!=render::SpriteSelection::world_only
                && pass.sprites!=render::SpriteSelection::configurable_hud_only)
            || (pass.scroll && pass.layer!=PpuLayer::bg2))
            throw std::invalid_argument("Invalid 3DS PPU source pass");
    }
}
}
PicaFrame PicaRaster::prepare(std::shared_ptr<const simulation::SnesPpuState> source,const PpuBatch& batch,
    const FramePlan& plan,unsigned brightness,unsigned subtract) {
    if(!source) throw std::invalid_argument("Missing immutable 3DS PPU snapshot");
    validate(*source,batch,plan,brightness,subtract);
    const auto width=batch.expand_horizontal?top_width+guard*2:256U;
    const int origin=int((width-256)/2);
    bool decode=!indexed_ || batch!=batch_;
    if(!decode && source_!=source)
        for(const auto& pass:batch.passes) if(!same_source(*source_,*source,pass)) {decode=true;break;}
    auto next=std::unique_ptr<render::Framebuffer>{};
    if(decode) {
        next=std::make_unique<render::Framebuffer>(width,native_height);
        next->enable_layer_tags(true);next->begin_write_coverage();
        const render::BackgroundRenderer backgrounds;const render::SpriteRenderer sprites;
        for(const auto& pass:batch.passes) {
            const bool extend=batch.expand_horizontal && pass.extend_horizontal;
            const auto priority=pass.priority<0?render::TilePriorityPass::all:
                pass.priority?render::TilePriorityPass::high:render::TilePriorityPass::low;
            const render::ScopedLayer tag(*next,pass.layer==PpuLayer::bg2?render::PixelLayer::background:render::PixelLayer::two_d);
            switch(pass.layer) {
            case PpuLayer::bg1:
                backgrounds.draw_bg1(*source,*next,priority,origin,extend,
                    pass.guard_inset,pass.transparent_black,pass.mosaic_inset);break;
            case PpuLayer::bg2: {
                const auto scroll=pass.scroll.value_or(std::array{source->bg2_scroll_x,source->bg2_scroll_y});
                backgrounds.draw_bg2(*source,scroll[0],scroll[1],*next,priority,origin,extend,
                    pass.wrap_horizontal,pass.transparent_black,pass.single_occurrence_top_rows);break;
            }
            case PpuLayer::bg3: backgrounds.draw_bg3(*source,*next,priority,origin,extend);break;
            case PpuLayer::objects:
                sprites.draw_objects(*source,*next,pass.priority<0?std::nullopt:std::optional<std::uint8_t>(pass.priority),
                    origin,extend,false,nullptr,false,nullptr,pass.sprites);break;
            }
        }
        next->end_write_coverage();
    }
    const auto& bitmap=decode?*next:*indexed_;
    const bool recolour=decode || palette_!=source->cgram || brightness_!=brightness || subtract_!=subtract;
    auto pixels=std::vector<std::uint8_t>{};bool visible=visible_;
    if(recolour) {
        pixels.assign(std::size_t(width)*screen_height*4,0);visible=false;
        std::array<std::array<std::uint8_t,3>,256> normal{},background{};
        for(unsigned ink=0;ink<normal.size();++ink) {
            normal[ink]=colour(source->cgram[ink],brightness,0);
            background[ink]=colour(source->cgram[ink],brightness,subtract);
        }
        for(unsigned y=0;y<screen_height;++y) for(unsigned x=0;x<width;++x) {
            const int logical_y=int(y)-8;
            if(batch.space==PicaSpace::screen && (logical_y<0 || logical_y>=int(native_height))) continue;
            const unsigned sy=unsigned(std::clamp(logical_y,0,int(native_height-1)));
            const auto offset=std::size_t(sy)*width+x;
            if(sy<batch.first_row || sy>=batch.last_row || !bitmap.write_coverage()[offset]) continue;
            const auto ink=bitmap.pixels()[offset];
            const auto& rgb=bitmap.layer_tags()[offset]==unsigned(render::PixelLayer::background)?background[ink]:normal[ink];
            const auto out=(std::size_t(y)*width+x)*4;
            std::copy(rgb.begin(),rgb.end(),pixels.begin()+out);pixels[out+3]=255;visible=true;
        }
    }
    // All allocating work precedes publication. Shader/native upload errors
    // are the presenter's responsibility; no failed source decode is published.
    auto next_batch=batch;
    if(decode) {indexed_=std::move(next);++work_.decodes;}
    if(recolour) {rgba_=std::move(pixels);++work_.colour_updates;}
    batch_=std::move(next_batch);source_=std::move(source);palette_=source_->cgram;
    brightness_=brightness;subtract_=subtract;visible_=visible;
    const float left=(float(top_width)-width)*.5F;
    constexpr std::array<std::array<float,2>,4> uv{{{0,0},{1,0},{1,1},{0,1}}};
    unsigned vertex=0;
    for(unsigned corner:{0U,1U,2U,0U,2U,3U})
        vertices_[vertex++]={{left+uv[corner][0]*width,uv[corner][1]*screen_height,0},{1,1,1,1},uv[corner]};
    draws_[0]={0,6,0,pica_identity,batch.space,false,false,true};
    images_[0]={rgba_,width,screen_height,width*4,4};
    return {plan,visible_?std::span<const PicaVertex>(vertices_):std::span<const PicaVertex>{},
        visible_?std::span<const PicaDraw>(draws_):std::span<const PicaDraw>{},
        visible_?std::span<const PicaImage>(images_):std::span<const PicaImage>{}};
}
} // namespace starfox::platform::nintendo_3ds

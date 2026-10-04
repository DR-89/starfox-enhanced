#include "starfox/platform/nintendo_3ds/pica_raster.hpp"

namespace starfox::platform::nintendo_3ds {
namespace {
constexpr unsigned native_height=224;
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
        || (batch.water_receiver && (ppu.background_mode!=1 || batch.space!=PicaSpace::scenery
            || batch.passes.empty() || std::any_of(batch.passes.begin(),batch.passes.end(),
                [](const auto& pass){return pass.layer!=PpuLayer::bg2;})))
        || (batch.space!=PicaSpace::screen && batch.space!=PicaSpace::scenery)
        || (batch.space==PicaSpace::scenery && !batch.expand_horizontal))
        throw std::invalid_argument("Unsupported/incomplete 3DS PPU painter group");
    if(plan.eye_count!=(plan.stereo?2U:1U)) throw std::invalid_argument("Invalid 3DS PPU eye plan");
    for(unsigned eye=0;eye<plan.eye_count;++eye) {
        static_cast<void>(PicaProjection(plan,eye));
    }
    for(const auto& pass:batch.passes) {
        if((pass.layer!=PpuLayer::bg1 && pass.layer!=PpuLayer::bg2 && pass.layer!=PpuLayer::bg3 && pass.layer!=PpuLayer::objects)
            || pass.priority< -1 || pass.priority>(pass.layer==PpuLayer::objects?3:1)
            || pass.guard_inset>128 || pass.single_occurrence_top_rows>512
            || (pass.single_occurrence_sky_half && (pass.layer!=PpuLayer::bg2 || ppu.background_mode!=2
                || !batch.expand_horizontal || !pass.extend_horizontal || !pass.single_occurrence_sky_half->rows
                || pass.single_occurrence_sky_half->rows>512))
            || (batch.space==PicaSpace::scenery && pass.layer==PpuLayer::objects)
            || (pass.sprites!=render::SpriteSelection::all && pass.sprites!=render::SpriteSelection::world_only
                && pass.sprites!=render::SpriteSelection::configurable_hud_only)
            || (pass.scroll && pass.layer!=PpuLayer::bg2))
            throw std::invalid_argument("Invalid 3DS PPU source pass");
    }
}
}
PicaFrame PicaRaster::prepare(std::shared_ptr<const simulation::SnesPpuState> source,const PpuBatch& batch,
    const FramePlan& plan,unsigned brightness,unsigned subtract,unsigned receiver_guard,bool trim_transparent) {
    if(!source) throw std::invalid_argument("Missing immutable 3DS PPU snapshot");
    validate(*source,batch,plan,brightness,subtract);
    const auto guard=batch.space==PicaSpace::scenery?std::max(pica_scenery_guard(plan),receiver_guard):pica_raster_base_guard;
    if(guard>(pica_raster_max_width-top_width)/2)
        throw std::invalid_argument("3DS source raster exceeds horizontal storage");
    auto width=batch.expand_horizontal?top_width+guard*2:256U;
    bool decode=!indexed_ || batch!=batch_;
    if(!decode && source_!=source)
        for(const auto& pass:batch.passes) if(!same_source(*source_,*source,pass)) {decode=true;break;}
    // Reuse already sufficient coverage during slider-only presentations.
    // A genuinely changed source pass can retire excess decoded storage.
    if(!decode) width=std::max(width,unsigned(indexed_->width()));
    decode=decode || indexed_->width()!=width;
    std::array<unsigned,pica_raster_max_strips+1> boundaries{};unsigned pages=0;
    while(boundaries[pages]<width) {
        if(pages==pica_raster_max_strips) throw std::length_error("3DS source strip count exceeded");
        const auto remaining=width-boundaries[pages];
        // Water receivers may just cross a power-of-two padding boundary.
        // Borrow another native-width strip instead of doubling its allocation;
        // the last of at most four strips can keep a non-power-of-two width.
        const auto span=(batch.water_receiver || batch.compact_strips) && remaining<=pica_raster_strip_width && pages+1<pica_raster_max_strips
            ?std::bit_floor(remaining):std::min(pica_raster_strip_width,remaining);
        boundaries[pages+1]=boundaries[pages]+span;++pages;
    }
    const int origin=int((width-256)/2);
    auto next=std::unique_ptr<render::Framebuffer>{};
    if(decode) {
        next=std::make_unique<render::Framebuffer>(width,native_height);
        next->enable_layer_tags(true);next->begin_write_coverage();
        const render::BackgroundRenderer backgrounds;const render::SpriteRenderer sprites;
        for(const auto& pass:batch.passes) {
            const bool extend=batch.expand_horizontal && pass.extend_horizontal;
            const auto priority=pass.priority<0?render::TilePriorityPass::all:
                pass.priority?render::TilePriorityPass::high:render::TilePriorityPass::low;
            // Internal tags deliberately differ from shared PixelLayer::two_d
            // (1), which SpriteRenderer applies inside its own scoped pass.
            // Do not modify the shared renderer or infer OBJ from palette ink.
            const auto bg_bit=pass.layer==PpuLayer::bg1?1:pass.layer==PpuLayer::bg2?2:4;
            const render::ScopedLayer tag(*next,static_cast<render::PixelLayer>(64|bg_bit));
            switch(pass.layer) {
            case PpuLayer::bg1:
                backgrounds.draw_bg1(*source,*next,priority,origin,extend,
                    pass.guard_inset,pass.transparent_black,pass.mosaic_inset);break;
            case PpuLayer::bg2: {
                const auto scroll=pass.scroll.value_or(std::array{source->bg2_scroll_x,source->bg2_scroll_y});
                render::BackgroundUniqueRegion region{};
                std::span<const render::BackgroundUniqueRegion> unique;
                if(pass.single_occurrence_sky_half) {
                    const int half=int(((source->bg2_screen_size&1)?64U:32U)*(source->bg2_tile_size_16?16U:8U)/2);
                    region={pass.single_occurrence_sky_half->right?half:0,0,
                        pass.single_occurrence_sky_half->right?half*2:half,int(pass.single_occurrence_sky_half->rows),
                        0,255,0,half};
                    unique={&region,1};
                }
                backgrounds.draw_bg2(*source,scroll[0],scroll[1],*next,priority,origin,extend,
                    pass.wrap_horizontal,pass.transparent_black,pass.single_occurrence_top_rows,unique);break;
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
    auto pixels=std::vector<std::uint8_t>{},layers=std::vector<std::uint8_t>{};bool visible=visible_;
    auto occupied=occupied_;
    if(decode) for(auto& bounds:occupied) bounds={pica_raster_strip_width,screen_height,0,0};
    if(decode) layers.assign(std::size_t(width)*screen_height,0);
    if(recolour) {
        pixels.assign(std::size_t(width)*screen_height*4,0);visible=false;
        std::array<std::array<std::uint8_t,3>,256> normal{},background{};
        for(unsigned ink=0;ink<normal.size();++ink) {
            normal[ink]=colour(source->cgram[ink],brightness,0);
            background[ink]=colour(source->cgram[ink],brightness,subtract);
        }
        for(unsigned y=0;y<screen_height;++y) for(unsigned page=0;page<pages;++page)
            for(unsigned x=boundaries[page];x<boundaries[page+1];++x) {
            const int logical_y=int(y)-8;
            if(batch.space==PicaSpace::screen && (logical_y<0 || logical_y>=int(native_height))) continue;
            const unsigned sy=unsigned(std::clamp(logical_y,0,int(native_height-1)));
            const auto offset=std::size_t(sy)*width+x;
            if(sy<batch.first_row || sy>=batch.last_row || !bitmap.write_coverage()[offset]) continue;
            const auto ink=bitmap.pixels()[offset];
            const auto tag=bitmap.layer_tags()[offset];
            const auto layer=tag==unsigned(render::PixelLayer::two_d)?16U:unsigned(tag&63);
            if(!pica_source_layer(layer) || !layer) throw std::logic_error("Unclassified 3DS PPU source pixel");
            const auto& rgb=layer==2?background[ink]:normal[ink];
            const auto out=(std::size_t(y)*width+x)*4;
            std::copy(rgb.begin(),rgb.end(),pixels.begin()+out);pixels[out+3]=255;visible=true;
            if(decode) {
                layers[std::size_t(y)*width+x]=std::uint8_t(layer);
                auto& bounds=occupied[page];const auto local=x-boundaries[page];
                bounds[0]=std::min(bounds[0],local);bounds[1]=std::min(bounds[1],y);
                bounds[2]=std::max(bounds[2],local+1);bounds[3]=std::max(bounds[3],y+1);
            }
        }
    }
    // All allocating work precedes publication. Shader/native upload errors
    // are the presenter's responsibility; no failed source decode is published.
    auto next_batch=batch;
    if(decode) {indexed_=std::move(next);layers_=std::move(layers);occupied_=occupied;++work_.decodes;}
    if(recolour) {rgba_=std::move(pixels);++work_.colour_updates;}
    batch_=std::move(next_batch);source_=std::move(source);palette_=source_->cgram;
    brightness_=brightness;subtract_=subtract;visible_=visible;
    const float left=(float(top_width)-width)*.5F;
    constexpr std::array<std::array<float,2>,4> uv{{{0,0},{1,0},{1,1},{0,1}}};
    unsigned strips=0;
    for(unsigned page=0;page<pages;++page) {
        const auto start=boundaries[page];
        auto bounds=std::array<unsigned,4>{0,0,boundaries[page+1]-start,screen_height};
        // A split screen-space OBJ group can contain only one tiny sprite.
        // Borrow its occupied rectangle too; retaining a whole guarded LCD
        // page per priority needlessly consumes the water compositor budget.
        // Geometry retains the exact source origin, including opaque black.
        if(trim_transparent) bounds=occupied_[page];
        if(bounds[0]>=bounds[2] || bounds[1]>=bounds[3]) continue;
        const unsigned size=bounds[2]-bounds[0],height=bounds[3]-bounds[1];
        unsigned vertex=strips*6;
        for(unsigned corner:{0U,1U,2U,0U,2U,3U})
            vertices_[vertex++]={{left+start+bounds[0]+uv[corner][0]*size,bounds[1]+uv[corner][1]*height,0},{1,1,1,1},uv[corner]};
        draws_[strips]={strips*6,6,strips,pica_identity,batch.space,false,false,true};
        const auto offset=std::size_t(bounds[1])*width+start+bounds[0];
        images_[strips]={std::span<const std::uint8_t>(rgba_).subspan(offset*4),size,height,width*4,4,false,
            std::span<const std::uint8_t>(layers_).subspan(offset),width};
        ++strips;
    }
    // Isolated artwork retains its actual one-hot ownership when its A8
    // descriptor is omitted. In water scenes the distant sky is BG3, not BG2.
    if(!batch.passes.empty() && batch.passes.front().layer!=PpuLayer::objects
        && std::all_of(batch.passes.begin(),batch.passes.end(),[&](const auto& pass) {
            return pass.layer==batch.passes.front().layer;
        })) {
        const auto layer=batch.passes.front().layer;
        for(unsigned i=0;i<strips;++i) draws_[i].source_layer=layer==PpuLayer::bg1?1:layer==PpuLayer::bg2?2:4;
    }
    return {plan,visible_?std::span<const PicaVertex>(vertices_).first(strips*6):std::span<const PicaVertex>{},
        visible_?std::span<const PicaDraw>(draws_).first(strips):std::span<const PicaDraw>{},
        visible_?std::span<const PicaImage>(images_).first(strips):std::span<const PicaImage>{}};
}
} // namespace starfox::platform::nintendo_3ds

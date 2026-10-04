#include "starfox/platform/nintendo_3ds/game_menu.hpp"
#include <iostream>

using namespace starfox;
using namespace starfox::platform::nintendo_3ds;
namespace {
unsigned checks{};
void require(bool value,const char* message) {++checks;if(!value) throw std::runtime_error(message);}
template<class F> void rejects(F run) {
    bool rejected=false;try {run();} catch(const std::exception&) {rejected=true;}
    require(rejected,"Malformed menu/ownership request accepted");
}
assets::RomImage public_font_fixture() {
    // Deliberately synthetic glyphs, not copied cartridge assets. The actual
    // ScaledTextRenderer still decodes its ordinary ROM width/translation data.
    std::vector<std::uint8_t> bytes(0x8000);
    for(unsigned i=0;i<96;++i) {
        bytes[0x100+i]=6;bytes[0x200+i]=std::uint8_t(i);
        for(unsigned row=0;row<12;++row) {bytes[0x300+i*24+row*2]=0xfc;bytes[0x300+i*24+row*2+1]=0;}
    }
    return assets::RomImage(std::move(bytes));
}
}
int main() try {
    const auto rom=public_font_fixture();
    const auto symbols=assets::SymbolMap::parse("MSCALECHARS $008000\nMARIOMSGS $008020\nFONT0WID $008100\nFONT0TRN $008200\nFONT0FON $008300\nFACEDATA $009000\n");
    GameMenu menu(rom,symbols);
    std::vector<std::uint8_t> lower(bottom_width*screen_height*3);
    const ImageView dashboard{lower,bottom_width,screen_height,bottom_width*3};
    const auto plan=plan_frame(0,false,ScreenUse::setup);
    GameMenuState state;state.visible=true;state.title="STAR FOX ENHANCED";
    for(auto id:simulation::pregame_menu_order(simulation::PregamePage::main)) state.rows.push_back({id,"ROW","VALUE",true});
    state.selection=state.rows.front().id;
    require(menu.update(state),"Initial actual menu was not drawn");
    require(!menu.update(state) && menu.redraws()==1,"Unchanged setup redrew every frame");
    require(valid_image(menu.plain_view(),top_width,screen_height),"Plain setup LCD image invalid");
    Canvas upper(top_width);upper.image(0,0,menu.plain_view());
    require(std::equal(upper.view().pixels.begin(),upper.view().pixels.end(),menu.plain_view().pixels.begin()),"Upper menu capture changed source font pixels");
    Canvas lower_canvas;rejects([&]{lower_canvas.image(0,0,menu.plain_view());});
    auto frame=menu.frame(plan);validate_pica_frame(frame,dashboard);
    require(frame.vertices.size()==6 && frame.draws.size()==1 && frame.textures.size()==1,"Plain setup prepared a world scene");
    require(frame.draws[0].space==PicaSpace::screen && !frame.draws[0].depth_test
        && !frame.draws[0].depth_write && frame.draws[0].source_layer==0,"Setup joined source depth/colour math");
    require(pica_resident_texture_bytes(frame.textures[0])==512*256*4,"Setup texture size exceeds native budget");
    for(std::size_t i=3;i<frame.textures[0].pixels.size();i+=4) require(frame.textures[0].pixels[i]==255,"Plain setup leaked background scene pixels");
    const auto pixels=std::vector<std::uint8_t>(frame.textures[0].pixels.begin(),frame.textures[0].pixels.end());
    for(float slider:{0.F,.25F,1.F,0.F}) {
        const auto next=menu.frame(plan_frame(slider,true,ScreenUse::menu_preview));
        validate_pica_frame(next,dashboard);
        require(std::equal(next.textures[0].pixels.begin(),next.textures[0].pixels.end(),pixels.begin()),"Slider changed menu pixels");
        for(unsigned eye=0;eye<next.plan.eye_count;++eye)
            require(pica_draw_matrix(next.plan,eye,next.draws[0])==pica_screen_matrix(top_width),"Screen menu acquired stereo disparity");
    }
    state.preview=true;require(menu.update(state),"Preview UI opacity did not update");
    frame=menu.frame(plan_frame(1,true,ScreenUse::menu_preview));
    validate_pica_frame(frame,dashboard);rejects([&]{static_cast<void>(menu.plain_view());});
    const auto at=[&](unsigned x,unsigned y){return frame.textures[0].pixels[(std::size_t(y)*top_width+x)*4+3];};
    require(at(0,239)==0 && at(200,233)==190 && at(8,20)==255,"Preview panel/text/background coverage wrong");
    for(const auto page:{simulation::PregamePage::main,simulation::PregamePage::options,simulation::PregamePage::two_d,
        simulation::PregamePage::three_d,simulation::PregamePage::cheats,simulation::PregamePage::global,simulation::PregamePage::stereo}) {
        state.page=page;state.rows.clear();state.preview=false;
        for(auto id:simulation::pregame_menu_order(page)) state.rows.push_back({id,"ROW","UNAVAILABLE",false});
        for(const auto& row:state.rows) {
            state.selection=row.id;menu.update(state);validate_pica_frame(menu.frame(plan),dashboard);
            require(menu.state().rows==state.rows && menu.state().selection==row.id,"Scrolling replaced/reordered the actual source menu");
        }
    }
    for(unsigned language=0;language<6;++language) {
        state.language=std::uint8_t(language);state.title="OPTIONS";
        menu.update(state);validate_pica_frame(menu.frame(plan),dashboard);
    }
    const auto saved=menu.state();auto bad=saved;bad.selection=255;
    rejects([&]{menu.update(bad);});require(menu.state()==saved,"Invalid snapshot partly replaced menu");
    bad=saved;bad.rows.push_back(bad.rows.front());rejects([&]{menu.update(bad);});
    bad=saved;bad.language=6;rejects([&]{menu.update(bad);});
    state.visible=false;menu.update(state);
    frame=menu.frame(plan);
    require(frame.vertices.empty() && frame.draws.empty() && frame.textures.empty(),"Menu remained over cartridge flow");
    rejects([&]{static_cast<void>(menu.plain_view());});
    std::cout<<"3DS actual menu renderer: "<<checks<<" checks passed (synthetic public font/layout, not cartridge/device acceptance)\n";
} catch(const std::exception& error) {std::cerr<<"3DS actual menu renderer: "<<error.what()<<'\n';return 1;}

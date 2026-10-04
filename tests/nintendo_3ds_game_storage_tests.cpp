#include "starfox/platform/nintendo_3ds/game_storage.hpp"
#include "starfox/state/archive.hpp"
#include "starfox/state/container.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

using namespace starfox;
using namespace platform::nintendo_3ds;
namespace {
unsigned checks{};
void require(bool value,const char* message) {++checks;if(!value) throw std::runtime_error(message);}
template<class F> void rejects(F f) {bool rejected=false;try {f();}catch(const std::exception&){rejected=true;}require(rejected,"Unsafe SD request accepted");}
struct Temporary {
    std::filesystem::path path;
    Temporary() {
        const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
        for(unsigned i=0;i<64;++i) {
            const auto candidate=std::filesystem::temp_directory_path()/("sfe-3ds-journal-"+std::to_string(stamp)+"-"+std::to_string(i));
            if(std::filesystem::create_directory(candidate)) {path=candidate;return;}
        }
        throw std::runtime_error("Cannot create isolated journal test directory");
    }
    ~Temporary() {
        // Only the two fixed files created by this fixture, then its own empty
        // directory. Never recursively remove a caller-selected location.
        std::error_code ignored;
        for(unsigned i=0;i<2;++i) std::filesystem::remove(path/("3ds-save-"+std::to_string(i)+".dat"),ignored);
        std::filesystem::remove(path,ignored);
    }
};
std::vector<std::uint8_t> bytes(const std::string& path) {
    std::ifstream file(path,std::ios::binary);if(!file) throw std::runtime_error("Missing test slot");
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
void replace(const std::string& path,std::span<const std::uint8_t> data) {
    std::ofstream file(path,std::ios::binary|std::ios::trunc);file.exceptions(std::ios::badbit|std::ios::failbit);
    file.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));file.close();
}
// Independent format oracle; do not use the production encoder/decoder.
std::vector<std::uint8_t> envelope(std::uint64_t generation,const GameSaveData& data,std::uint32_t manifest) {
    state::Writer writer;const auto& p=data.preferences;
    writer(generation,data.experience,data.preview,p.timing,p.music,p.sfx,p.language,p.laser,p.level,
        p.swap,p.god,p.bombs,p.boost,p.lives,p.planet_cheat,p.separation,p.convergence,data.ex_rom_crc,data.ex_sram);
    return state::pack(0x33445301U,manifest,writer.bytes());
}
GameSaveData fixture() {
    GameSaveData data;data.experience=simulation::Experience::starfox_ex;data.preview=true;
    data.preferences={simulation::TimingMode::unlocked_20_fps,35,65,4,2,66,true,true,true,true,true,true,32,4096};
    data.ex_rom_crc=0x12345678;data.ex_sram.resize(GameStorage::ex_sram_bytes);
    for(std::size_t i=0;i<data.ex_sram.size();++i) data.ex_sram[i]=std::uint8_t(i*73U+19U);
    return data;
}
void normal_and_recovery() {
    Temporary temp;constexpr std::uint32_t manifest=0x81234567;
    GameStorage store(temp.path.generic_string(),manifest);
    rejects([&]{store.save({});});rejects([&]{static_cast<void>(store.slot_path(2));});
    const auto initial=store.load();require(!initial.found && initial.writable && initial.warning.empty(),"First boot was mistaken for damaged saves");
    GameSaveData first;require(store.save(first) && store.generation()==1,"First settings update failed");
    require(bytes(store.slot_path(0))==envelope(1,first,manifest),"Stored settings differ from independent field/envelope oracle");
    const auto second=fixture();require(store.save(second) && store.generation()==2,"EX settings/SRAM not committed");
    require(bytes(store.slot_path(1))==envelope(2,second,manifest),"Stored EX bank differs from independent oracle");
    require(bytes(store.slot_path(0))==envelope(1,first,manifest),"EX commit truncated the previous valid slot");
    const auto old0=bytes(store.slot_path(0)),old1=bytes(store.slot_path(1));
    require(!store.save(second) && store.generation()==2,"Unchanged configuration was rewritten");
    require(bytes(store.slot_path(0))==old0 && bytes(store.slot_path(1))==old1,"No-op commit changed SD files");
    GameStorage reopened(temp.path.generic_string(),manifest);
    require(reopened.load().found && reopened.current().data==second && reopened.generation()==2,"Process reopen lost settings/EX SRAM");
    auto third=second;third.preferences.music=55;require(reopened.save(third) && reopened.generation()==3,"Alternating update failed");
    require(bytes(store.slot_path(1))==old1,"Third update overwrote the newest valid backup");
    auto partial=bytes(store.slot_path(0));partial.resize(7);replace(store.slot_path(0),partial);
    GameStorage recovered(temp.path.generic_string(),manifest);
    require(recovered.load().data==second && recovered.current().found && recovered.current().writable
        && !recovered.current().warning.empty() && recovered.generation()==2,"Partial write did not recover the older valid slot");
    require(recovered.save(second) && recovered.generation()==3 && recovered.current().warning.empty(),"Damaged alternate slot could not be repaired without truncating backup");
    require(bytes(store.slot_path(1))==old1,"Recovery altered intact backup");
    // Losing only the newest file still leaves the preceding generation.
    std::filesystem::remove(store.slot_path(0));GameStorage missing(temp.path.generic_string(),manifest);
    require(missing.load().found && missing.current().data==second,"One missing slot discarded the remaining save");
    // A stale owner must not overwrite a newer owner's data when it changes.
    require(missing.save(third),"Write after one missing slot failed");
    auto fourth=third;fourth.preferences.sfx=45;rejects([&]{store.save(fourth);});
    require(bytes(store.slot_path(0))==envelope(3,third,manifest),"Stale owner clobbered newer save");
    const auto latest=bytes(store.slot_path(0)),backup=bytes(store.slot_path(1));
    GameStorage other_assets(temp.path.generic_string(),manifest^1U);
    const auto wrong=other_assets.load();require(!wrong.found && !wrong.writable && !wrong.warning.empty(),"Incompatible bundle save was treated as a fresh default");
    rejects([&]{other_assets.save({});});
    require(bytes(store.slot_path(0))==latest && bytes(store.slot_path(1))==backup,"Incompatible manifest overwrote original save");
}
void invalid_and_io() {
    Temporary temp;constexpr std::uint32_t manifest=0x1234;
    GameStorage store(temp.path.generic_string(),manifest);static_cast<void>(store.load());const auto good=fixture();store.save(good);
    const auto stable=bytes(store.slot_path(0));
    for(unsigned field=0;field<10;++field) {
        auto bad=good;
        switch(field) {
        case 0:bad.preferences.music=101;break;case 1:bad.preferences.sfx=101;break;
        case 2:bad.preferences.language=6;break;case 3:bad.preferences.laser=3;break;
        case 4:bad.preferences.separation=0;break;case 5:bad.preferences.separation=65;break;
        case 6:bad.preferences.convergence=15;break;case 7:bad.preferences.level=10;break;
        case 8:bad.experience=static_cast<simulation::Experience>(-1);break;
        case 9:bad.preferences.timing=static_cast<simulation::TimingMode>(-1);break;
        }
        rejects([&]{store.save(bad);});require(bytes(store.slot_path(0))==stable,"Invalid settings touched a valid slot");
        replace(store.slot_path(1),envelope(2,bad,manifest));GameStorage untrusted(temp.path.generic_string(),manifest);
        require(untrusted.load().found && untrusted.current().data==good && !untrusted.current().warning.empty(),"Invalid decoded fields replaced valid backup");
    }
    auto bad=good;bad.ex_sram.pop_back();rejects([&]{store.save(bad);});
    bad=good;bad.ex_sram.clear();rejects([&]{store.save(bad);});
    // A failed write to an alternate slot cannot replace the current file.
    std::filesystem::remove(store.slot_path(1));std::filesystem::create_directory(store.slot_path(1));
    auto changed=good;changed.preferences.music=50;rejects([&]{store.save(changed);});
    require(bytes(store.slot_path(0))==stable && store.generation()==1,"Write failure advanced generation or damaged backup");
    std::filesystem::remove(store.slot_path(1));
    // Both corrupt => defaults can be used, but existing files remain read-only.
    const std::array<std::uint8_t,3> garbage{1,2,3};replace(store.slot_path(0),garbage);replace(store.slot_path(1),garbage);
    GameStorage corrupt(temp.path.generic_string(),manifest);const auto loaded=corrupt.load();
    require(!loaded.found && !loaded.writable && !loaded.warning.empty(),"All-corrupt journal silently allowed default overwrite");
    rejects([&]{corrupt.save(good);});require(bytes(store.slot_path(0))==bytes(store.slot_path(1)),"All-corrupt files changed during refusal");
    require(bytes(store.slot_path(0))==std::vector<std::uint8_t>(garbage.begin(),garbage.end()),"All-corrupt journal was deleted");
    // Size is checked before allocating/decoding the input.
    std::vector<std::uint8_t> large(GameStorage::maximum_file_bytes+1,0);replace(store.slot_path(0),large);
    std::filesystem::remove(store.slot_path(1));GameStorage oversized(temp.path.generic_string(),manifest);
    require(!oversized.load().writable && !oversized.current().found,"Oversized journal accepted");
    replace(store.slot_path(0),envelope(0,good,manifest));GameStorage zero(temp.path.generic_string(),manifest);
    require(!zero.load().writable,"Zero-generation journal accepted");
    replace(store.slot_path(0),envelope(std::numeric_limits<std::uint64_t>::max(),good,manifest));
    GameStorage full(temp.path.generic_string(),manifest);require(full.load().found,"Last representable generation rejected");
    rejects([&]{full.save(changed);});require(full.generation()==std::numeric_limits<std::uint64_t>::max(),"Generation wrapped");
    replace(store.slot_path(0),envelope(5,good,manifest));replace(store.slot_path(1),envelope(5,changed,manifest));
    GameStorage ambiguous(temp.path.generic_string(),manifest);require(!ambiguous.load().writable,"Equal-generation conflicting saves accepted");
    GameStorage no_directory((temp.path/"missing").generic_string(),manifest);static_cast<void>(no_directory.load());rejects([&]{no_directory.save({});});
    require(!std::filesystem::exists(temp.path/"missing"),"Native save silently created an unintended directory");
    rejects([&]{GameStorage invalid("",manifest);});
}
}
int main() try {
    normal_and_recovery();invalid_and_io();
    std::cout<<"3DS SD settings/EX SRAM journal: "<<checks<<" checks passed; synthetic public saves, not physical SD power-loss acceptance\n";
} catch(const std::exception& error) {std::cerr<<"3DS SD journal: "<<error.what()<<'\n';return 1;}

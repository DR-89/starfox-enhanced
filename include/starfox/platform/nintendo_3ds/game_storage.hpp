#pragma once
#include "starfox/platform/nintendo_3ds/game_session.hpp"
#include <string>

namespace starfox::platform::nintendo_3ds {
struct GameSaveData {
    simulation::Experience experience{simulation::Experience::original};
    bool preview{};
    GamePreferences preferences;
    std::uint32_t ex_rom_crc{};
    std::vector<std::uint8_t> ex_sram;
    bool operator==(const GameSaveData&) const=default;
};
struct GameSaveLoad {
    GameSaveData data;
    bool found{},writable{true};
    std::string warning;
};
// One native owner, two checksummed SD slots. Write the older/incomplete slot,
// close and re-read it before committing the in-memory generation. The newest
// valid slot is never truncated by an update. This is interruption recovery,
// not an SD-card/controller power-loss durability or multi-writer guarantee.
class GameStorage {
public:
    GameStorage(std::string directory,std::uint32_t companion_manifest);
    [[nodiscard]] const GameSaveLoad& load();
    // False means the exact data is already saved; no disk write occurs.
    // All-corrupt/incompatible slots are preserved and require explicit user
    // recovery outside the app. Never silently overwrite them with defaults.
    bool save(const GameSaveData&);
    [[nodiscard]] const GameSaveLoad& current() const noexcept {return current_;}
    [[nodiscard]] std::uint64_t generation() const noexcept {return generation_;}
    [[nodiscard]] std::string slot_path(unsigned slot) const;
    static constexpr std::size_t ex_sram_bytes=65'536;
    static constexpr std::size_t maximum_file_bytes=ex_sram_bytes+256;
private:
    std::string directory_;
    std::uint32_t manifest_;
    GameSaveLoad current_;
    std::optional<unsigned> newest_;
    std::uint64_t generation_{};
    bool initialized_{};
};
} // namespace starfox::platform::nintendo_3ds

#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

// A single pilot's Kill Board row. See WC3GameFlow's kill-board rendering
// for how these get displayed (CALLSIGN/ACES/KILLS columns + a live total).
struct WC3PilotStats {
    std::string name;
    int aces{0};
    int kills{0};
};

// One numbered Duty Logs save slot's contents. Format on disk is a
// `FORM WCSV` IFF file (see WC3SaveGame.cpp for the reader/writer),
// following this codebase's existing hand-rolled IFF chunk-loop idiom
// (WC3Scene::parseGump, WC3BranchPak, etc.) for reading, and IFFWriter for
// writing:
//   HEAD — currentScene (i32 LE), room id (4-byte string, not
//          null-terminated).
//   NAME — player-chosen save name (raw bytes, no length prefix/nul — the
//          chunk's own IFF size is the length), typed in on the Duty Logs
//          page's "save" flow. Absent in files written before this existed
//          (or if the player left it blank); readers leave `name` empty.
//   VARB — flat (bankLetter:u8 'A'|'B', slot:i32 LE, value:i32 LE) triples.
//          Only a small allowlist of slots are dumped — most bank-A/B state
//          (e.g. the roster-availability flags 143-149) is deterministically
//          re-derived by loadScene()/room INIT and doesn't need saving; see
//          kSavedBankASlots/kSavedBankBSlots in WC3SaveGame.cpp. Best-effort:
//          extend that allowlist as more relevant slots are identified,
//          rather than trying to capture every possible flag up front.
//   ROST — pilotCount (u32 LE), then that many (nameLen:u8, name:bytes,
//          aces:i32 LE, kills:i32 LE) records.
//   LOAD — loadoutShipIndex (i32 LE), hardpointCount (u32 LE), then that
//          many (missileTypeIndex:i32 LE, loadedCount:i32 LE) pairs for the
//          selected ship's own hardpoints only (see WC3LoadoutData.h) —
//          switching craft type resets to that craft's defaults rather than
//          remembering configs for all five simultaneously, since only the
//          selected one is ever actually flown. Absent in files written
//          before Loadout persistence existed — readers leave both fields
//          at their defaults (ship 0, empty hardpoint list) when missing.
//   MRAL — shipMorale (i32 LE), then 8 (morale:i32 LE, rosterOverride:u8,
//          relationshipStatus:i32 LE) triples, one per WC3GameFlow::WC3Pilot
//          enum value in order (Flint, Flash, Cobra, Hobbes, Vagabond,
//          Vaquero, Maniac, Rachel — see that enum's own comment).
//          rosterOverride is a WC3GameFlow::WC3RosterOverride value
//          (0=None, 1=ForceUnavailable, 2=ForceAvailable) and is always
//          0/None for Rachel (never a flight-roster pilot). relationshipStatus
//          is the Flint/Rachel love-triangle endgame-companion tracker (see
//          WC3GameFlow::m_pilotRelationshipStatus's own comment: 0=undetermined,
//          13=chosen, 2=passed over) and is only ever non-zero for Flint/
//          Rachel's own slots. Both stored as plain int/uint8_t here rather
//          than WC3GameFlow's enum/array types so this header doesn't need
//          to include WC3GameFlow.h. Absent in files written before branch-
//          choice consequences existed — readers leave all values at their
//          defaults (0 morale, None override, 0 relationship status) when
//          missing, same convention as the LOAD chunk above.
struct WC3LoadoutHardpointState {
    int missileTypeIndex{0};
    int loadedCount{0};
};
struct WC3SaveGame {
    int currentScene{0};
    std::string roomId;
    std::string name; // player-chosen save name, see NAME chunk above
    std::unordered_map<int, int32_t> bankA; // only the saved-slot allowlist
    std::unordered_map<int, int32_t> bankB;
    std::vector<WC3PilotStats> pilots;
    int loadoutShipIndex{0};
    std::vector<WC3LoadoutHardpointState> loadoutHardpoints;
    int shipMorale{0};
    std::array<int, 8> pilotMorale{};
    std::array<uint8_t, 8> pilotRosterOverride{};
    std::array<int, 8> pilotRelationshipStatus{};
};

// Slot paths are "saves/wc3save<N>.dat" for N in [1, kNumSaveSlots].
constexpr int kNumSaveSlots = 20;
std::string saveSlotPath(int slotNumber); // 1-based

// Full load/save of one slot.
bool loadSaveGame(int slotNumber, WC3SaveGame& out);
bool writeSaveGame(int slotNumber, const WC3SaveGame& in);

// Cheap "peek just the HEAD/NAME chunks" for the Duty Logs slot list —
// doesn't load VARB/ROST. Returns false (and leaves the out-params
// untouched) if the slot file doesn't exist. outName is left empty if the
// slot has no NAME chunk (e.g. saved with a blank name).
bool peekSaveGameHeader(int slotNumber, int& outScene, std::string& outRoomId, std::string& outName);

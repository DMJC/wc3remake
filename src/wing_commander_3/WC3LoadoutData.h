#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Real per-craft/per-hardpoint/per-weapon data model for the Loadout screen
// (room 0010), parsed directly from GAMEFLOW/LOADOUT.IFF — see
// WC3LoadoutData.cpp for the confirmed on-disk layout (headless-decoded,
// not guessed). This superseded an earlier hand-written placeholder table;
// every field below is real game data.

// One weapon/equipment type — WING/ARMY record order in LOADOUT.IFF matches
// 184.SHP's (big icon) and 185.SHP's (small hardpoint icon, weapons 0-6
// only — see WC3GameFlow::renderLoadoutPage) frame order exactly, so a
// weapon's index in WC3LoadoutDatabase::weapons IS its shape frame.
struct WC3WeaponDef {
    std::string name;         // MAIN, e.g. "DUMBFIRE", "TEMBLOR BOMB"
    std::string weaponClass;  // SUBS "TYPE:" line, e.g. "Missile", "Torpedo", "Mine", "CLASSIFIED"
    std::string tacval;       // SUBS "TACVAL:" line, e.g. "Low", "CAP SHIP", "ULTRA"
    std::string objt;         // in-flight object model name (absent for the two CLASSIFIED specials)
    uint32_t mask{0};         // this weapon's own bit (single bit, e.g. 1, 2, 4, ... 256)
    // TAGS' two i32LE fields, real data — byte-confirmed as (25, 2) for
    // every one of the 9 real weapon records with no exceptions, unlike
    // WC3CraftDef::tag0 below. tag1 matches craft's own constant-7 pattern
    // (a per-record-type marker, "2"=weapon vs "7"=craft, still a guess at
    // exact meaning) but tag0 doesn't vary at all here, so there's nothing
    // to correlate it against yet. Captured rather than discarded per user
    // instruction (2026-07 session: "don't ignore any of them").
    int32_t tag0{0};
    int32_t tag1{0};
};

// One hardpoint on a craft. mask is a bitwise-OR of the WC3WeaponDef::mask
// values this hardpoint may carry (e.g. 95 = DUMBFIRE|HEATSEEK|IMAGE REC|
// FRIEND OR FOE|LEECH|PORCUPINE) — there is no separate "special hardpoint"
// flag anywhere in the real data: a hardpoint whose mask matches only one
// weapon (e.g. Excalibur's Temblor Bomb/Cloaking Device bays) is simply a
// hardpoint with one compatible weapon, handled the same way as any other.
// cordX/cordY are the hardpoint's real absolute screen-space position (same
// 640x480 space as WC3Gump::x/y) for its small icon on the ship render —
// not a layout guess, read directly from the file's CORD chunk.
struct WC3HardpointDef {
    uint32_t mask{0};
    int capacity{1};
    int cordX{0};
    int cordY{0};
    // STAT's own i32LE field — read 0 in every one of the 31 real
    // hardpoint records in LOADOUT.IFF (a static save file with nothing
    // yet equipped), consistent with it being a runtime "currently loaded
    // weapon index/count" slot the static file just ships zeroed. Captured
    // rather than skipped per user instruction (2026-07 session: "don't
    // ignore any of them") even though its value never varies here.
    int32_t stat{0};
};

// One craft loadout record. LOADOUT.IFF has 8 of these (WING/NUM_ == 8),
// one per 183.SHP frame in file order — records 4-7 are all named
// "EXCALIBUR" but are 4 genuinely distinct hardpoint layouts (base, +Temblor
// Bomb+Cloak, +Temblor Bomb only, +Cloak only), matching 183.SHP frames 4-7
// exactly. A craft's index in WC3LoadoutDatabase::craft IS its 183.SHP frame.
struct WC3CraftDef {
    std::string name;                    // MAIN
    std::vector<std::string> specLines;  // SUBS, split on its embedded NULs (TYPE/ARMOR/SHIELD/SPEED/SERVICE/ARCHITECT)
    std::string objt;                    // in-flight object model name
    int decoyCount{0};                   // DECY
    std::vector<WC3HardpointDef> hardpoints;
    // TAGS' two i32LE fields, real data captured rather than discarded
    // (2026-07 session, user instruction: "don't ignore any of them").
    // tag1 is a constant 7 across all 8 real craft records (plausibly a
    // record-type marker, paired with WC3WeaponDef::tag1's own constant 2
    // for weapon records — "7"=craft/"2"=weapon is a guess, not confirmed).
    // tag0 varies (40/40/45/70/40/40/40/40 for ARROW/HELLCAT V/
    // THUNDERBOLT VII/LONGBOW/EXCALIBUR x4) and roughly tracks each craft's
    // real-world size class (Longbow is the bomber, Thunderbolt the heavy
    // fighter) — but doesn't match any single visible SUBS stat (armor/
    // shield/speed) exactly for any craft checked, so its precise meaning
    // (mass? point cost? UI layout hint?) is still unconfirmed.
    int32_t tag0{0};
    int32_t tag1{0};
};

// 185.SHP UI-chrome frames (no corresponding weapon) — the small hardpoint
// icon has no small-icon art for weapon indices 7/8 (TEMBLOR BOMB/CLOAKING
// DEVICE), so those just fall back to the blank frame.
constexpr int kHardpointIconBlank        = 7; // empty/not-applicable slot
constexpr int kHardpointIconSelectedSlot = 8; // current slot, drawn on top as a highlight

// Parses LOADOUT.IFF and answers the couple of derived questions the
// Loadout screen actually needs (which weapons fit a given hardpoint).
struct WC3LoadoutDatabase {
    std::vector<WC3CraftDef> craft;
    std::vector<WC3WeaponDef> weapons;
    bool loaded{false};

    bool loadFromBytes(const uint8_t* data, size_t size);

    // Every weapons[] index whose mask bit is set in hp.mask, in
    // weapons[] (== shape frame) order — what the missile-type cycle
    // arrows step through for that hardpoint.
    std::vector<int> compatibleWeapons(const WC3HardpointDef& hp) const;
    // First (lowest-index) compatible weapon — the default load for a
    // freshly-selected craft.
    int firstCompatibleWeapon(const WC3HardpointDef& hp) const;
};

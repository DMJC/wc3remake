//
//  RSEntity.h
//  libRealSpace
//
//  Created by fabien sanglard on 12/29/2013.
//  Copyright (c) 2013 Fabien Sanglard. All rights reserved.
//

#pragma once
#include <stdint.h>
#include <vector>

#include "AssetManager.h"
#include "../commons/IFFSaxLexer.h"
#include "../commons/Maths.h"
#include "../commons/Matrix.h"
#include "../commons/Quaternion.h"
#include "../commons/PKWareDecompressor.h"
#include "RSImage.h"

#include "TreArchive.h"
#include "../commons/LZBuffer.h"
#define LOD_LEVEL_MAX 0
#define LOD_LEVEL_MED 1
#define LOD_LEVEL_MIN 2

class RSImageSet;

typedef struct MapVertex {
    Point3D v;

    uint8_t flag;
    uint8_t type;
    uint8_t lowerImageID;
    uint8_t upperImageID;

    float color[4];

} MapVertex;

typedef struct BoudingBox {
    Point3D min;
    Point3D max;
} BoudingBox;

typedef struct UV {
    uint8_t u;
    uint8_t v;
} UV;

typedef struct uvxyEntry {

    uint16_t triangleID;
    uint16_t textureID;
    UV uvs[3];
} uvxyEntry;

typedef struct qmapuvxyEntry {
    uint16_t triangleID;
    uint16_t textureID;
    UV uvs[4];
} qmapuvxyEntry;

typedef struct Triangle {

    uint8_t property;
    uint16_t ids[3];

    uint8_t color;
    uint8_t flags[3];

} Triangle;

typedef struct Quads {

    uint8_t property;
    uint16_t ids[4];

    uint8_t color;
    uint8_t flags[3];

} Quads;

typedef struct Lod {

    uint32_t dist;
    uint16_t numTriangles;
    uint16_t triangleIDs[16336];
} Lod;

typedef struct Attr {
    uint16_t id;
    char type;
    uint8_t props1;
    uint8_t props2;
} Attr;

enum EntityType {
    ground = 1,
    jet = 2,
    ornt = 3,
    swpn = 4,
    aftb = 5,
    missiles = 6,
    bomb = 7,
    tracer = 8,
    explosion = 9,
    object_mobile = 10,
    debris = 11,
    destroyed_object = 12,
    rnwy = 13,
    podr = 14,
    // Distant backdrop billboard (OBJT>DIST) — e.g. WC3's galaxy skybox
    // textures (DARKGAL1.IFF, COMET.IFF, ...) referenced by a mission's own
    // WRLD>SKYS chunk. A single flat quad + one texture, via APPR>DFLT
    // (same VERT/TXMS/TXMP/QUAD sub-chunk layout as APPR>POLY).
    dist = 15
};
class RSImage;

struct VGAPalette;


struct HPTS {
    uint8_t id;
    // Ship-local mount offset, same 8-bit-fractional fixed-point encoding as
    // APPR>POLY>VERT (ByteStream::ReadFixedFloatLE) -- confirmed by cross-
    // checking real hardpoint records against their own ship's VERT bounding
    // box (e.g. ARROW.IFF: raw GUNS record x=192/z=4544/y=-688 is nonsense
    // at face value against a ~40-unit-wide hull, but decodes to a sane
    // (0.75, -2.69, 17.75) once run through the same fixed-point conversion
    // as every other position field in this format).
    float x;
    float y;
    float z;
};

typedef struct JDYN {
    uint32_t FUEL;
    uint16_t U1;
    uint16_t C1;
    uint16_t C2;
    uint32_t U2;
    uint32_t U3;
    uint16_t ROLL_RATE;
    uint16_t ROLL_RATE_MAX;
    uint16_t CS3_qqch_lift;
    uint16_t CS4;
    uint32_t U5;
    uint32_t U6;
    uint16_t U7;
    uint16_t U8;
    uint32_t LIFT_SPD;
    uint32_t DRAG;
    uint32_t LIFT;
    uint8_t aileron;
    uint8_t gouverne;
    uint8_t MAX_G;
    uint16_t U13;
    uint16_t TWIST_RATE;
    uint16_t TWIST_RATE_MAX;
    uint32_t U16;
    uint16_t U17;
    // WC3 SSHP>DYNM>FGTR's own 3rd rotation-rate field (see
    // RSEntity::parseREAL_OBJT_SSHP_DYNM_FGTR) — previously read and
    // discarded entirely. Despite their field names above (inherited from
    // this struct's original SC1/JETP shape), a manual-stat cross-check
    // (Arrow/Excalibur/Hellcat V) confirms ROLL_RATE above actually holds
    // pitch dps, TWIST_RATE actually holds yaw dps, and THIS field is the
    // real roll dps. Only ever populated for WC3 SSHP ships.
    uint16_t WC3_TRUE_ROLL_RATE_DPS{0};
} JDYN;

class RSEntity {

    struct CHLD {
        std::string name;
        int32_t x;
        int32_t y;
        int32_t z;
        std::vector<uint8_t> data;
        RSEntity *objct;
    };
    struct EXPL {
        std::string name;
        int16_t x;
        int16_t y;
        RSEntity *objct;
        // SSHP>EXPL>DATA (and, since the handler is shared, MISL>EXPL>DATA
        // too — JETP's own separate EXPL uses x/y above instead): 2
        // int32LE strength/radius-ish parameters, 8.8 fixed-point (divide
        // by 256). Scale confirmed for the SSHP case specifically; not
        // separately re-verified for MISL's smaller values (e.g.
        // HSMISS.IFF's "MISSPOOF" explosion reads 20/20 raw — plausible
        // either scaled or not, given how small a missile's own blast
        // would be relative to a full ship's). Scale confirmed against our own previously-recorded raw
        // value for a ship's main explosion (25600/25600) landing on
        // exactly 100/100 once scaled — a clean round number, and matching
        // an independent decoder's documented example
        // (/home/james/development/wc-iff-loader) of a different ship's
        // "EXPLODE" explosion also being 100/100 — a corroborating match
        // against our own data, not a claim taken on trust alone. Which
        // field is "strength" vs "radius" isn't confirmed, just that /256
        // is the right scale.
        float strength1{0};
        float strength2{0};
    };
    struct WDAT {
        uint16_t damage{0};
        uint16_t radius{0};
        uint8_t unknown1{0};
        uint8_t weapon_id{0};
        uint8_t weapon_category{0};
        uint8_t radar_type{0};
        uint8_t weapon_aspec{0};
        uint32_t target_range{0};
        uint8_t tracking_cone{0};
        uint32_t effective_range{0};
        uint8_t unknown6{0};
        uint8_t unknown7{0};
        uint8_t unknown8{0};
        // WC3 OBJT>GUNS>DATA fields (parseREAL_OBJT_GUNS_DATA) — SC1's own
        // MISS>WDAT (the only other populator of this struct) has no
        // equivalent, so these stay at their defaults (0) for SC1 weapons.
        // refire_delay_seconds: real seconds between shots, confirmed via
        // a real Tachyon Gun data point (user-supplied: damage 70/energy
        // 40/range 3200/refire 0.35s) against the file's raw stored value
        // 89 -- 89/256 = 0.3477, matching the same /256 fixed-point scale
        // already established elsewhere in this codebase
        // (MISN_AREA_POSITION_SCALE). energy_cost: real per-shot cost
        // against the ship's own gun_energy_capacity/_recharge_rate pool
        // (RSEntity), confirmed exact (no scaling) against the same
        // Tachyon Gun data point. unknown_gun_stat: the one GUNS>DATA
        // field with no confirmed meaning yet (offset 23-26 — 512 for
        // Tachyon Gun) — stored raw, unused, pending a real data point.
        float refire_delay_seconds{0.0f};
        uint32_t energy_cost{0};
        uint32_t unknown_gun_stat{0};
        // WC3 OBJT>MISL>DATA fields (parseREAL_OBJT_MISL_DATA) — see that
        // function's own comment for the full, up-to-date confirmation
        // status of each field (damage and lock_time_required_seconds are
        // solid across 4 real data points; duration_seconds is NOT — see
        // below). lock_time_required_seconds generalizes what was
        // previously only a category==3 (torpedo/T-bomb) concept
        // (SCenums.h's wc3_weapon_stats placeholder table) to every WC3
        // missile — SCPlane::Shoot() now checks this directly when set
        // (nonzero, or the weapon is category 3) instead of the
        // placeholder table.
        // duration_seconds's own byte position turned out NOT to reliably
        // hold flight duration once checked against more than 2 weapons
        // (see parseREAL_OBJT_MISL_DATA's own comment) — kept as a raw,
        // unreliable value; effective_range (derived from it in
        // getWC3RealWeaponEntity) is correspondingly a rough estimate for
        // torpedo/capital-ship-class weapons specifically.
        float lock_time_required_seconds{0.0f};
        float duration_seconds{0.0f};
        // offset 23-26 of OBJT>MISL>DATA — no confirmed meaning yet, stored
        // raw (see parseREAL_OBJT_MISL_DATA's own comment for sampled
        // values).
        uint32_t unknown_misl_stat{0};
        // Offset 16 of OBJT>MISL>DATA ("flag1", previously discarded
        // unread) — user-confirmed real behavior (2026-07 session): mines
        // are fired from missile hardpoints but drop from the ship and
        // stay stationary instead of flying out like a normal missile.
        // Byte-confirmed across all 6 real WC3 guided-weapon files:
        // MINEMISS is the only one with this byte 0 (every other missile/
        // torpedo/T-bomb checked has it 1) — read as a "flies under its
        // own guidance" flag, so false means "drop and stay put" (a mine).
        bool is_guided_flight{true};
    };
    struct SWPN_DATA {
        std::string weapon_name;
        int32_t weapons_round{0};
        int32_t detection_range{0};
        int32_t effective_range{0};
        uint16_t unknown1{0};
        uint16_t unknown2{0};
        uint16_t unknown3{0};
        uint8_t max_simultaneous_shots{0};
        uint16_t weapons_round2{0};
        int32_t unknown4{0};
        RSEntity *weapon_entity{nullptr};
    };
    struct DYNN_MISS {
        uint32_t turn_degre_per_sec{0};
        uint32_t velovity_m_per_sec{0};
        uint32_t proximity_cm{0};
    };
    // OBJT>MISL>DATA: WC3's own missile/torpedo/bomb weapon definitions
    // (HSMISS.IFF "HS MISSILE", IRMISS.IFF "IMREC MISSILE", FFMISS.IFF
    // "IFF MISSILE", TORKMISS.IFF "TORPEDO", TEMBMISS.IFF "T-BOMB", ...).
    // A completely distinct, unrelated chunk from OBJT>MISS (Strike
    // Commander's own missile format, parsed by parseREAL_OBJT_MISS below) —
    // different 4-char tag, different layout, and until now, entirely
    // unparsed (IFFSaxLexer had no "MISL" handler registered at all, so
    // every WC3 missile's own stats were silently skipped).
    // name(16, null-padded) + 3 flag-looking bytes + 3x uint32 + 1 uint16 +
    // 2 pad bytes = 35 bytes. Field meanings are a best-effort guess from
    // magnitude alone (not independently confirmed): stat1 looks like a
    // damage value (T-BOMB's is exactly 1,000,000 — plausible for the
    // superweapon that destroys Kilrah's fault line); stat2/stat3/stat4
    // are smaller values in the tens-to-thousands range, plausibly
    // proximity/tracking/detection-range figures, but not pinned down.
    struct MISL_DATA {
        std::string name;
        uint8_t flag1{0};
        uint8_t flag2{0};
        uint8_t flag3{0};
        uint32_t stat1_damage{0};
        uint32_t stat2{0};
        uint32_t stat3{0};
        uint16_t stat4{0};
    };
    struct RADAR_SIGN {
        uint8_t unknown1{0};
        uint8_t unknown2{0};
        uint8_t unknown3{0};
    };
    // SSHP>AFTB: the named subobject (e.g. "AFBURN2") mounted at each engine
    // position — see WEAPS/EXPL above for the same name+objct pattern. The
    // subobject entity itself uses APPR>POLY>DETA with exactly 8 levels
    // (LVL0-LVL7): LVL0-LVL6 are the 7 throttle-percentage thrust-glow
    // states (0%..100%), LVL7 is the afterburner-engaged flame.
    struct AFTB {
        std::string name;
        RSEntity *objct{nullptr};
        std::vector<Point3D> positions;
    };
    // SSHP>DEBR: one piece of wreckage spawned on destruction, from
    // DATA\OBJECTS\<name>.IFF. Byte grouping below (type/size/offset/
    // velocity_range as 4 groups of 3 i16s after 2 leading bytes) is
    // corroborated independently: the constant values we'd already
    // recorded for these fields (-25,-25,-25,-10,-10,-10) split cleanly
    // into two distinct constant triplets at exactly these offsets,
    // matching an "offset" then "velocity_range" grouping rather than one
    // arbitrary 6-tuple. The final x/y/z remains an unconfirmed guess —
    // could be a spawn position OR (per an independent decoder at
    // /home/james/development/wc-iff-loader, itself not validated on this
    // point either) a launch velocity; not resolved either way.
    struct DEBR_PIECE {
        std::string name;
        int16_t type{0};
        int16_t size_x{0}, size_y{0}, size_z{0};
        int16_t offset_x{0}, offset_y{0}, offset_z{0};
        int16_t velocity_range_x{0}, velocity_range_y{0}, velocity_range_z{0};
        int32_t x{0};
        int32_t y{0};
        int32_t z{0};
        RSEntity *objct{nullptr};
    };
    // SSHP>SHLD: DATA names the shield-hit visual effect entity (e.g.
    // "SHLDFX"); FGTR/CPTL carries per-quadrant shield hitpoints — front/
    // back/left/right were already confirmed/in use before this round.
    // regen_rate was a new, NOT independently validated addition based on a
    // layout guess from an independent decoder
    // (/home/james/development/wc-iff-loader).
    //
    // The chunk is actually 36 bytes (9 uint32 fields), not the 32 (8
    // fields) previously read — confirmed by decoding HELLCAT.IFF's raw
    // SHLD>FGTR bytes directly: 44, 440, 440, 440, 440, 100, 80, 100, 80.
    // Cross-referenced against the WC3 manual's own printed Hellcat stats
    // (Shields 220cm; Armor Fore/Aft 100cm, Right/Left 80cm — provided by
    // the user): the previously-unread 9th field (the trailing 80) is part
    // of a SEPARATE armor quadruple that was being silently truncated away
    // by the old `size < 32` cutoff, not "3 arc-width fields" as previously
    // guessed. The armor quadruple's raw sequence (100, 80, 100, 80) only
    // matches the manual's Fore/Aft=100 & Right/Left=80 pairing if read
    // INTERLEAVED (front, side, back, side) rather than in block order
    // (front, back, left, right) — Fore and Aft are two slots apart in the
    // raw bytes, not adjacent. Which of the two "side" slots is left vs.
    // right is NOT determined by this (every ship in the manual has
    // Left==Right, so no sample can disambiguate); armor_side_a/side_b are
    // named neutrally until that's confirmed some other way. This also
    // raises (but does NOT resolve — Hellcat's own shield quadruple is
    // symmetric, 440 on all four sides, so it can't disambiguate its own
    // order) the question of whether the shield front/back/left/right
    // fields below are actually block-ordered as their names claim, or
    // secretly interleaved the same way the armor quadruple turned out to
    // be.
    //
    // Independently cross-checked against a second, unrelated ship: capital
    // ships use SHLD>CPTL instead of SHLD>FGTR (same 36-byte layout, same
    // parser — see parseREAL_OBJT_SSHP_SHLD_FGTR). VICTORY.IFF's raw CPTL
    // bytes decode to armor (1000, 1000, 1000, 1000), matching the user's
    // manual figures for the Destroyer/Cruiser/Light Carrier classes
    // (Fore/Aft 1000, Left/Right 1000) exactly.
    struct SHLD_FX {
        std::string name;
        RSEntity *objct{nullptr};
        uint32_t regen_rate{0};
        uint32_t front{0};
        uint32_t back{0};
        uint32_t left{0};
        uint32_t right{0};
        uint32_t armor_front{0};
        uint32_t armor_side_a{0};
        uint32_t armor_back{0};
        uint32_t armor_side_b{0};
    };
    // REAL>OBJT>{SSHP,MISL}>CLOK>DATA — cloaking-device data. Found on
    // STRAKHA.IFF (base tier — Kilrathi stealth fighter, matches WC3
    // lore), EXCALK.IFF (the cloak-equipped Excalibur finale variant —
    // notably NOT on EXCALP/EXCALT, the other two variants, consistent
    // with "K" specifically meaning cloak-capable), and SKIPMISS.IFF (the
    // "Skipper Missile" stealth guided torpedo — user-confirmed 2026-07
    // session real name). 20 bytes on SSHP entities (5x int32 LE), 24 on
    // MISL (SKIPMISS has a 6th trailing field). First 4 fields are
    // byte-identical across all three files checked (16, -41, -32, 32) —
    // plausibly shared cloak-effect visual constants (fade timing?), not
    // independently confirmed. The 5th field differs per entity in a way
    // that scales with how long each thing's own real-game cloak duration
    // should intuitively be (Excalibur 25 > Strakha 10 > missile 5) —
    // treated here as a duration in seconds. Confirmed exactly right for
    // SKIPMISS (user-confirmed real behavior: cloaks every 5 seconds,
    // matching this field precisely — see SCMissionActors::UpdateCloak).
    // Strakha's own duration (10) has no equivalent confirmation yet.
    // SKIPMISS's 6th field (1792) has no cross-reference at all yet.
    struct CLOK_DATA {
        int32_t field0{0};
        int32_t field1{0};
        int32_t field2{0};
        int32_t field3{0};
        // Best-effort "cloak duration" in seconds — see struct comment.
        int32_t duration{0};
        // Only present on MISL entities (SKIPMISS); -1 if the chunk was
        // the 20-byte SSHP-style variant with no 6th field.
        int32_t field5{-1};
    };
    // Capital ships (WEAP>CPTL>TURT, e.g. VICTORY.IFF) list gun turrets
    // instead of fixed hardpoints: a mount/base mesh, a separate gun/barrel
    // mesh, a weapon-type name (not a mesh — no DATA\OBJECTS\<name>.IFF
    // resolution attempted for it, matching GUNS/MISL's own weapon-type
    // strings), and a world-relative-to-ship mount position. pitch/yaw/
    // elevation_cap confirmed via cross-check against an independent
    // decoder (/home/james/development/wc-iff-loader): pitch and yaw are
    // raw int32LE divided by 256 to get degrees, elevation_cap is a raw
    // (unscaled) degree value, typically 90 but varies by ship/mount
    // (e.g. every KCARRIER.IFF turret uses 60, likely flight-deck
    // obstruction). Confirmed empirically: mirrored left/right turret
    // pairs share the same pitch/elevation_cap with yaw sign flipped.
    struct TURRET {
        std::string mount_name;
        std::string gun_name;
        std::string weapon_type;
        RSEntity *mount_objct{nullptr};
        RSEntity *gun_objct{nullptr};
        // Same 8-bit-fractional fixed-point encoding as APPR>POLY>VERT's own
        // vertex positions (ByteStream::ReadFixedFloatLE) — confirmed
        // against an independent decoder (/home/james/development/
        // wc-iff-loader), which divides these same raw ints by 256.0f.
        float x{0};
        float y{0};
        float z{0};
        float pitch{0};
        float yaw{0};
        int32_t elevation_cap{90};
    };
    // WEAP>CPTL>CRGO: decorative/functional objects sitting on a capital
    // ship's own hangar deck (parked fighters, crates, a truck) —
    // user-confirmed (2026-07 session). 37-byte records, name(8) confirmed
    // (every name in VICTORY.IFF is <=8 chars, cleanly null-padded —
    // "arr_h", "medf_h", "box7", "truck", ...). Of the 29-byte tail, two
    // fields decode cleanly, both found by systematically scanning every
    // byte offset across all 12 of VICTORY.IFF's real records for clean
    // round numbers instead of noise:
    //  - record-offset 17, ReadFixedFloatLE (same 8-bit-fractional fixed-
    //    point convention as APPR>POLY>VERT/TURT): 0.0 for every record
    //    the user describes as parked straight in a bay row, but +135.0
    //    for "tbol_h" (confirmed by the user to sit on the side of the
    //    hangar with no bay slots, i.e. genuinely not row-aligned) and
    //    -135.0 for both "medf_h" records that the user separately
    //    confirmed are "two diagonally parked hellcats next to a truck".
    //    This is a real per-instance orientation/yaw field — stored as
    //    `yaw` below and applied as a render-time rotation.
    //  - record-offset 20, same encoding: constant per cargo TYPE (not
    //    per instance) — 200.0 for arr_h/tbol_h/medf_h, 100.0 for
    //    drm_h/drm_h2/box7/box8/truck. First guess was "a per-type
    //    height/clearance above the deck floor" — ruled out by live
    //    testing plus a direct check of the numbers: VICTEST3.IFF's own
    //    real bounding box is only 147 units tall floor-to-ceiling, so
    //    adding 200 (or even 100) on top of the floor lands at/above the
    //    ceiling, not on the floor, which is exactly the "models above
    //    the flight deck" symptom reported. So deckOffsetY is real,
    //    clean, confirmed data — just not a Y height; its true meaning is
    //    still unknown. Kept parsed but not used for placement.
    //  These two fields overlap by one raw byte (offset17's field is
    //  bytes 17-20, offset20's is bytes 20-23) — byte 20 is genuinely the
    //  sign/top byte of the yaw field AND the fractional/bottom byte of
    //  the type-constant field simultaneously. Both readings independently
    //  produce clean, semantically-fitting values across all 12 records,
    //  so both are parsed as real despite the odd packing; see the .cpp
    //  for how ByteStream::MoveBackward is used to re-read the shared byte.
    //  - record-offset 29, same encoding: a real per-instance position
    //    field, found (2026-07 session) by comparing KCARRIER.IFF's CRGO
    //    against VICTORY.IFF's — user pointed out KCARRIER has straight
    //    rows of Dralthi/Darket fighters lined up on its flight deck,
    //    and every one of KCARRIER's 13 records is byte-identical except
    //    this window, which steps by an exact constant (200.0) between
    //    consecutive fighters. VICTORY.IFF's own bay-row trio
    //    (arr_h/medf_h/arr_h) steps by a constant -126.0 the same way,
    //    and the diagonally-parked hellcat pair + the truck extend
    //    smoothly past the row's end (-880, -1060, -1152) — i.e. exactly
    //    "next to a truck", with no hand-tuned placement needed. Values
    //    also sit comfortably inside VICTEST3.IFF's own real X extent
    //    (-1375.53 to 1348.93), confirming this is a direct local-space
    //    coordinate along the deck's long axis, not a separately-scaled
    //    value. Stored as `rowOffset` below and used directly as the
    //    render-space X coordinate. Still open: no field for the OTHER
    //    axis (which wall a bay/cluster sits against) has been found —
    //    that's still approximated via yaw/deckOffsetY grouping, see
    //    SCRenderer::drawModel's cargo block.
    struct CARGO_OBJECT {
        std::string name;
        RSEntity *objct{nullptr};
        float deckOffsetY{0.0f};
        float yaw{0.0f};
        float rowOffset{0.0f};
    };


public:
    struct WEAPS {
        int nb_weap;
        std::string name;
        RSEntity *objct;
    };

    std::vector<RSImage *> images;
    std::vector<Point3D> vertices;
    std::vector<uvxyEntry> uvs;
    std::vector<qmapuvxyEntry *> qmapuvs;
    std::vector<Lod> lods;
    std::unordered_map<uint16_t, Attr *> attrs;
    std::vector<Triangle> triangles;
    std::vector<Quads *> quads;
    std::vector<WEAPS *> weaps;
    std::vector<HPTS *> hpts;
    std::vector<CHLD *> chld;
    enum Property { SC_TRANSPARENT = 0x02 };
    EXPL *explos{nullptr};
    AFTB *afterburner{nullptr};
    std::vector<DEBR_PIECE *> debris;
    SHLD_FX *shield{nullptr};
    CLOK_DATA *clok{nullptr};
    std::string decoy_name;
    RSEntity *decoy_entity{nullptr};
    // WEAP>FGTR>DECY's leading u32 — confirmed carried decoy count (not a
    // deploy interval, as previously guessed): matches known in-game values
    // exactly (Confederation fighters noticeably higher — Arrow 16, Hellcat
    // 24, Excalibur 30 — than Kilrathi fighters, which cluster at 6-8).
    uint32_t decoy_count{0};
    // WEAP>FGTR>GUNS's 9-byte header: flag(1) + max gun energy(u32) +
    // recharge rate(u32). Confirmed against live gameplay: predicted
    // recharge time (gun_energy_capacity / (gun_energy_recharge_rate/5.0))
    // comes out to 10.0s for HELLCAT.IFF and 10.6s for HELLCATP.IFF, matching
    // an observed "~10-11 seconds to fully recharge from empty" almost
    // exactly — the /5 factor's own origin isn't confirmed (possibly a
    // fixed-point scale or an engine tick-rate constant), just the ratio
    // needed to convert the raw value into a real per-second rate.
    // gun_flag is 0 on every ship checked except the three cloak-equipped
    // Excalibur finale variants (EXCALK/EXCALP/EXCALT), where it's 1 —
    // plausibly a cloak/variant marker, not confirmed.
    uint8_t gun_flag{0};
    uint32_t gun_energy_capacity{0};
    uint32_t gun_energy_recharge_rate{0};
    std::vector<TURRET *> turrets;
    // WEAP>CPTL>TURT's leading u32 header. Doesn't correlate with turret
    // count (e.g. KTANKER's 2 turrets and TDEST's 9 turrets both read 192)
    // or hull HP (DAMG>CPTL) by any consistent ratio, but does scale with
    // ship class/toughness (KDREAD, the biggest/toughest ship, has the
    // highest value at 800; the smallest ships cluster at 144-192) — fits
    // a per-turret armor/HP value applied uniformly across the whole
    // battery. Not independently confirmed against a specific known
    // number, just a plausible interpretation of the observed pattern.
    uint32_t turret_armor{0};
    std::vector<CARGO_OBJECT *> cargo;
    // APPR>POLY>SUPR: names a separate, flyable-into IFF model for a
    // capital ship's own hangar bay interior (e.g. VICTORY.IFF's own SUPR
    // names "VICTEST3" -> DATA\OBJECTS\VICTEST3.IFF). entry_point_index and
    // parent_index confirmed via cross-check against an independent
    // decoder (/home/james/development/wc-iff-loader).
    std::string flight_deck_name;
    RSEntity *flight_deck_entity{nullptr};
    uint16_t flight_deck_entry_point_index{0};
    int32_t flight_deck_parent_index{-1};
    int32_t thrust_in_newton{0};
    int32_t weight_in_kg{0};
    int32_t drag{0};
    RADAR_SIGN *radar_signature{nullptr};
    uint8_t target_type{0};
    uint8_t health{0};
    WDAT *wdat{nullptr};
    DYNN_MISS *dynn_miss{nullptr};
    MISL_DATA *misl_data{nullptr};
    SWPN_DATA *swpn_data{nullptr};
    // OBJT>GUNS>DATA/OBJT>MISL>DATA's own display name field ("Tachyon
    // Gun", "Laser", "IFF MISSILE", ...) — kept separate from this->name
    // (the resolved asset filename, set by InitFromRAM's caller) rather
    // than overwriting it.
    std::string weapon_display_name;
    RSEntity *destroyed_object{nullptr};
    std::string destroyed_object_name;
    std::string cockpit_name;
    bool gravity{false};
    float wing_area{0};
    JDYN *jdyn{nullptr};
    uint16_t life{0};
    std::unordered_map<std::string, std::unordered_map<std::string, uint16_t>> sysm;
    // For rendering
    Point3D position;
    Quaternion orientation;
    std::vector<RSImageSet *> images_set;
    std::vector<Texture *> animations;
    // Has the entity been sent to te GPU and is ready to be renderer.

    bool prepared{false};
    // Set on RSEntity::flight_deck_entity (APPR>POLY>SUPR, e.g. VICTEST3.IFF):
    // a capital ship's hangar-bay interior is a corridor meant to be viewed
    // from inside it, unlike every other model in this engine (ships,
    // turrets, ...) which is always viewed from outside. SCRenderer's
    // FixEntityWinding() needs to know this to flip its outward-facing-normal
    // convention, or every wall/beam face ends up pointing away from the
    // camera and gets entirely backface-culled.
    bool is_interior_geometry{false};
    AssetManager &assetsManager = AssetManager::instance();
    std::string name;
    ~RSEntity();

    void InitFromRAM(uint8_t *data, size_t size, std::string name);
    size_t NumImages(void);
    size_t NumVertice(void);
    size_t NumUVs(void);
    size_t NumLods(void);
    size_t NumTriangles(void);
    inline bool IsPrepared(void) { return this->prepared; }
    BoudingBox *GetBoudingBpx(void);
    EntityType entity_type;
    void parseREAL_OBJT_JETP(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_WEAP(uint8_t *data, size_t size);
    BoudingBox bb;
private:
    void CalcBoundingBox(void);
    void calcWingArea(void);
    void AddImage(RSImage *image);
    void AddVertex(Point3D *vertex);
    void AddUV(uvxyEntry *uv);
    void AddLod(Lod *lod);
    void AddTriangle(Triangle *triangle);

    void parseREAL(uint8_t *data, size_t size);
    void parseREAL_INFO(uint8_t *data, size_t size);
    void parseREAL_OBJT(uint8_t *data, size_t size);
    void parseREAL_OBJT_INFO(uint8_t *data, size_t size);
    void parseREAL_OBJT_GRND(uint8_t *data, size_t size);
    void parseREAL_OBJT_SWPN(uint8_t *data, size_t size);
    void parseREAL_OBJT_RNWY(uint8_t *data, size_t size);
    void parseREAL_OBJT_DIST(uint8_t *data, size_t size);
    void parseREAL_OBJT_ORNT(uint8_t *data, size_t size);
    void parseREAL_OBJT_MISS(uint8_t *data, size_t size);
    // WC3's own missile/torpedo/bomb weapon format — see the MISL_DATA
    // struct comment above.
    void parseREAL_OBJT_MISL(uint8_t *data, size_t size);
    void parseREAL_OBJT_MISL_DATA(uint8_t *data, size_t size);
    void parseREAL_OBJT_MISL_DYNM(uint8_t *data, size_t size);
    void parseREAL_OBJT_MISL_AFTB(uint8_t *data, size_t size);
    void parseREAL_OBJT_MISL_AFTB_DATA(uint8_t *data, size_t size);
    // WC3's standalone gun weapon files (OBJT>GUNS) — see the .cpp
    // definitions' own comment for the confirmed field layout. Not to be
    // confused with parseREAL_OBJT_SSHP_WEAP_FGTR_GUNS (a ship's own
    // hardpoint-position list).
    void parseREAL_OBJT_GUNS(uint8_t *data, size_t size);
    void parseREAL_OBJT_GUNS_DATA(uint8_t *data, size_t size);
    void parseREAL_OBJT_BOMB(uint8_t *data, size_t size);
    void parseREAL_OBJT_TRCR(uint8_t *data, size_t size); 
    void parseREAL_OBJT_AFTB(uint8_t *data, size_t size);
    void parseREAL_OBJT_EXPL(uint8_t *data, size_t size);
    void parseREAL_OBJT_SMKG(uint8_t *data, size_t size);
    void parseREAL_OBJT_OMOB(uint8_t *data, size_t size);
    void parseREAL_OBJT_DEBR(uint8_t *data, size_t size);
    void parseREAL_OBJT_PODR(uint8_t *data, size_t size);
    void parseREAL_OBJT_AFTB_APPR(uint8_t *data, size_t size);
    void parseREAL_OBJT_MISS_EXPL(uint8_t *data, size_t size);
    void parseREAL_OBJT_MISS_SIGN(uint8_t *data, size_t size);
    void parseREAL_OBJT_MISS_TRGT(uint8_t *data, size_t size);
    void parseREAL_OBJT_MISS_SMOK(uint8_t *data, size_t size);
    void parseREAL_OBJT_MISS_DAMG(uint8_t *data, size_t size);
    void parseREAL_OBJT_MISS_WDAT(uint8_t *data, size_t size);
    void parseREAL_OBJT_MISS_DATA(uint8_t *data, size_t size);
    void parseREAL_OBJT_MISS_DYNM(uint8_t *data, size_t size);
    void parseREAL_OBJT_MISS_DYNM_MISS(uint8_t *data, size_t size);
    void parseREAL_OBJT_MISS_DYNM_ATMO(uint8_t *data, size_t size);
    void parseREAL_OBJT_MISS_DYNM_AGRV(uint8_t *data, size_t size);
    void parseREAL_OBJT_MISS_DYNM_GRAV(uint8_t *data, size_t size);
    void parseREAL_OBJT_SWPN_DYNM(uint8_t *data, size_t size);
    void parseREAL_OBJT_SWPN_DATA(uint8_t *data, size_t size);
    void parseREAL_OBJT_SWPN_ALGN(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_INFO(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_EXPL(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_DEBR(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_DEST(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_SMOK(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_CHLD(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_JINF(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_DAMG(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_WEAP_DAMG_SYSM(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_EJEC(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_SIGN(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_TRGT(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_CTRL(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_CKPT(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_TOFF(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_LAND(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_DYNM(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_DYNM_DYNM(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_DYNM_ORDY(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_DYNM_STBL(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_DYNM_ATMO(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_DYNM_GRAV(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_DYNM_THRS(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_DYNM_JDYN(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_WEAP_INFO(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_WEAP_DCOY(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_WEAP_WPNS(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_WEAP_HPTS(uint8_t *data, size_t size);
    void parseREAL_OBJT_JETP_WEAP_DAMG(uint8_t *data, size_t size);
    // WC3's own fighters/bombers use OBJT>SSHP ("space ship"), never
    // OBJT>JETP (Strike Commander's atmospheric jets) — different tag,
    // different (much flatter) sub-chunk layout. See RSEntity.cpp for the
    // empirically reverse-engineered field mapping.
    void parseREAL_OBJT_SSHP(uint8_t *data, size_t size);
    void parseREAL_OBJT_SSHP_DYNM(uint8_t *data, size_t size);
    void parseREAL_OBJT_SSHP_DYNM_FGTR(uint8_t *data, size_t size);
    void parseREAL_OBJT_SSHP_DAMG(uint8_t *data, size_t size);
    void parseREAL_OBJT_SSHP_DAMG_FGTR(uint8_t *data, size_t size);
    void parseREAL_OBJT_SSHP_WEAP(uint8_t *data, size_t size);
    void parseREAL_OBJT_SSHP_WEAP_FGTR(uint8_t *data, size_t size);
    void parseREAL_OBJT_SSHP_WEAP_FGTR_GUNS(uint8_t *data, size_t size);
    void parseREAL_OBJT_SSHP_WEAP_FGTR_MISL(uint8_t *data, size_t size);
    // Helpers for the two parsers above — see their definitions in
    // RSEntity.cpp for the full comment (weapon-name -> weapon_ids
    // translation, and the real-asset-loading weapon cache). static (not
    // per-instance) since the cache is shared across every ship.
    static int wc3WeaponNameToId(const std::string &name);
    // Confirmed raw WC3 hardpoint type-id byte -> weapon_ids, for GUNS
    // records specifically (see its own comment in RSEntity.cpp for why
    // this is preferred over wc3WeaponNameToId there — the byte value was
    // independently reverse-engineered and confirmed stable per gun name
    // across every ship checked; the record's exact 8-byte name spelling
    // was never independently confirmed the same way).
    static int wc3GunRawIdToWeaponId(uint8_t rawId);
    // Resolves a WC3 weapon hardpoint's embedded name (e.g. "NEUTGUN",
    // "IRMISS") to its real DATA\OBJECTS\<NAME>.IFF asset — confirmed
    // directly (HELLCATP.IFF's own GUNS/MISL/DECY link to NEUTGUN.IFF/
    // ION_GUN.IFF/IRMISS.IFF/DECOY.IFF), the same way the sibling DECY
    // parser already resolves "DECOY" (see parseREAL_OBJT_SSHP_WEAP_FGTR_
    // DECY's own real-asset-loading code, RSEntity.cpp). Gives real 3D
    // geometry, unlike the placeholder-only synthetic entity this
    // replaced. wdat is still synthesized from wc3_weapon_stats
    // (SCenums.h) and attached after loading, keyed by weaponId — WC3
    // weapon files aren't confirmed to carry a WDAT-shaped stat chunk the
    // way SC1's MISS>WDAT does. Cached per weapon NAME (not weaponId),
    // since the real asset is shared regardless of which numeric id a
    // given hardpoint's raw byte happened to decode to.
    static RSEntity *getWC3RealWeaponEntity(const std::string &name, int weaponId);
    void parseREAL_OBJT_SSHP_WEAP_FGTR_DECY(uint8_t *data, size_t size);
    void parseREAL_OBJT_SSHP_WEAP_CPTL(uint8_t *data, size_t size);
    void parseREAL_OBJT_SSHP_WEAP_CPTL_TURT(uint8_t *data, size_t size);
    void parseREAL_OBJT_SSHP_WEAP_CPTL_CRGO(uint8_t *data, size_t size);
    void parseREAL_APPR_POLY_SUPR(uint8_t *data, size_t size);
    void parseREAL_OBJT_SSHP_EXPL(uint8_t *data, size_t size);
    void parseREAL_OBJT_SSHP_EXPL_DATA(uint8_t *data, size_t size);
    void parseREAL_OBJT_SSHP_DEBR(uint8_t *data, size_t size);
    void parseREAL_OBJT_SSHP_DEBR_DATA(uint8_t *data, size_t size);
    void parseREAL_OBJT_SSHP_AFTB(uint8_t *data, size_t size);
    void parseREAL_OBJT_SSHP_AFTB_DATA(uint8_t *data, size_t size);
    void parseREAL_OBJT_SSHP_SHLD(uint8_t *data, size_t size);
    void parseREAL_OBJT_SSHP_SHLD_DATA(uint8_t *data, size_t size);
    void parseREAL_OBJT_SSHP_SHLD_FGTR(uint8_t *data, size_t size);
    // Shared between SSHP and MISL (see CLOK_DATA's own comment) — both
    // entity kinds carry the same FORM CLOK > DATA shape.
    void parseREAL_OBJT_CLOK(uint8_t *data, size_t size);
    void parseREAL_OBJT_CLOK_DATA(uint8_t *data, size_t size);
    void parseREAL_APPR_POLY_TRIS_TXMS_TXMV(uint8_t *data, size_t size);
    // Transient scratch used only while parseREAL_OBJT_SSHP_SHLD's own two
    // sibling sub-chunks (DATA then FGTR) are being read.
    SHLD_FX *m_parsingShield{nullptr};
    void parseREAL_OBJT_EXTE(uint8_t *data, size_t size);
    void parseREAL_APPR(uint8_t *data, size_t size);
    void parseREAL_APPR_POLY(uint8_t *data, size_t size);
    void parseREAL_APPR_POLY_INFO(uint8_t *data, size_t size);
    void parseREAL_APPR_POLY_VERT(uint8_t *data, size_t size);
    void parseREAL_APPR_POLY_DETA(uint8_t *data, size_t size);
    void parseREAL_APPR_POLY_DETA_LVLX(uint8_t *data, size_t size);
    void parseREAL_APPR_POLY_ATTR(uint8_t *data, size_t size);
    void parseREAL_OBJT_PODR_DATA(uint8_t *data, size_t size);
    void parseREAL_APPR_POLY_TRIS(uint8_t *data, size_t size);
    void parseREAL_APPR_POLY_TRIS_LNTH(uint8_t *data, size_t size);
    void parseREAL_APPR_POLY_TRIS_VTRI(uint8_t *data, size_t size);
    void parseREAL_APPR_POLY_TRIS_FACE(uint8_t *data, size_t size);
    void parseREAL_APPR_POLY_TRIS_TXMS(uint8_t *data, size_t size);
    void parseREAL_APPR_POLY_TRIS_TXMS_INFO(uint8_t *data, size_t size);
    void parseREAL_APPR_POLY_TRIS_TXMS_TXMP(uint8_t *data, size_t size);
    void parseREAL_APPR_POLY_TRIS_TXMS_TXMA(uint8_t *data, size_t size);
    void parseREAL_APPR_POLY_TRIS_UVXY(uint8_t *data, size_t size);
    void parseREAL_APPR_POLY_TRIS_MAPS(uint8_t *data, size_t size);
    void parseREAL_APPR_POLY_QUAD(uint8_t *data, size_t size);
    void parseREAL_APPR_POLY_QUAD_FACE(uint8_t *data, size_t size);
    void parseREAL_APPR_POLY_QUAD_MAPS(uint8_t *data, size_t size);

    void parseREAL_APPR_ANIM(uint8_t *data, size_t size);
    void parseREAL_APPR_ANIM_INFO(uint8_t *data, size_t size);
    void parseREAL_APPR_ANIM_SEQU(uint8_t *data, size_t size);
    void parseREAL_APPR_ANIM_SHAP(uint8_t *data, size_t size);
};

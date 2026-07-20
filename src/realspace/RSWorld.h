//
//  RSWorld.h
//  libRealSpace
//
//  Created by Rémi LEONARD on 02/12/2024.
//  Copyright (c) 2013 Fabien Sanglard. All rights reserved.
//
#pragma once

#include "../commons/IFFSaxLexer.h"
#include "RSImageSet.h"

// One face of a mission's space-scene skybox: a named distant-billboard
// entity (e.g. "darkgal1" -> DATA\OBJECTS\DARKGAL1.IFF, an OBJT>DIST/
// APPR>DFLT single textured quad) plus the direction it should be placed
// along relative to the camera. WRLD's own SKYS chunk lists 6 of these
// (matching the 6 faces of a cube: (-1,0,0)/(1,0,0)/(0,1,0)/(0,-1,0)/
// (0,0,-1)/(0,0,1)) — 5 galaxy backdrops plus one "COMET" accent.
struct WorldSkyFace {
    std::string name;
    int32_t dirX{0};
    int32_t dirY{0};
    int32_t dirZ{0};
    int32_t distance{0};
};

// WRLD>ASTR: one scattered asteroid field. 108-byte records (4-byte leading
// count header on the chunk as a whole, not per-record), byte-verified by
// cross-comparing all 24 real WORLD.IFF-family files in missions.tre that
// carry this chunk (see session notes — every record ends with the same
// embedded ASCII model name, confirming this is a scatter-field definition,
// not e.g. per-asteroid entries). Several fields are confirmed constant
// across every sample and their purpose is still a guess; noted per-field
// below. Not wired up to any renderer yet — parsed but unused.
struct WorldAsteroidField {
    int32_t count{0};        // asteroids in the field; 100-3500 observed
    int32_t variantCount{0}; // 5 or 25 observed — plausibly distinct rock shape/size variants
    // 2 reserved int32s here always read 0 in every sample (not stored).
    int32_t centerX{0}, centerY{0}, centerZ{0}; // field center, world units
    // 3 back-to-back (x,y,z) int32 triples, always identical to each other
    // within a single record (e.g. 12800/2560/-5120) — likely a per-size-
    // class scatter volume, but why the SAME triple repeats 3x instead of
    // 3 distinct ones isn't understood.
    int32_t scatterVolume[3][3]{};
    int32_t unknown1{0};  // always 175 in every sample
    int32_t spread{0};    // varies 20000-300000 — plausibly a scatter radius/density param
    int32_t unknown2{0};  // always 20000 in every sample
    int32_t axisParam[3]{}; // 3 equal values per record, varies 10-30 across records
    int32_t unknown3{0};  // always 1
    // Trailing 16 bytes of the record, read as one null-terminated string.
    // Every sample seen reads "astroid1aster" (13 chars + 3 nulls) — i.e.
    // the model name "astroid1" (-> DATA\OBJECTS\ASTROID1.IFF) immediately
    // followed by 5 more printable bytes ("aster") with no null in between,
    // so this might really be two adjacent fields (an 8-byte name + a
    // separate 5-8 byte field that happens to also be printable) rather
    // than one 13-character name — the exact sub-field boundary here isn't
    // confirmed, so it's kept as a single raw string rather than guessing
    // a split.
    std::string modelNameRaw;
};

// WRLD>CAMR>AUTO: one camera/autopilot-mode sequence record (4 seen per real
// WORLD.IFF: TAKEOFF/LANDING/AUTOPILT/AUTOCAPS). Shares its header shape with
// CAMR's other children (TARG/ROTA/CHAS/COCK/VCTC/WEAP/TRAK) — see
// parseWRLD_CAMR_AUTO's own comment for what's confirmed there. tailRaw is
// everything past that shared header (real per-mode camera data, per a
// user-confirmed hunch this session) — kept as a raw blob rather than a
// guessed field split, since byte-diffing TAKEOFF vs LANDING only shows
// candidate position-like fields with an inconsistent pattern. Parsed but
// unused: no gameplay/rendering code reads tailRaw yet.
struct WorldCameraAutoSequence {
    std::string modeLabel;   // "TAKEOFF"/"LANDING"/"AUTOPILT"/"AUTOCAPS"
    std::string actorName;   // "PLAYER" in every sample checked
    int32_t sharedField0{0}; // always 50000
    int16_t sharedFovish{0}; // 40 typical / 30 for COCK-style records — plausibly FOV degrees
    int16_t sharedField1{0}; // always 2560
    int16_t sharedField2{0}; // always 0
    std::vector<uint8_t> tailRaw; // everything past the shared header, verbatim, undecoded
};

class RSWorld {
private:
    void parseWRLD(uint8_t *data, size_t size);
    void parseWRLD_INFO(uint8_t *data, size_t size);
    // User-confirmed: horizon data. See the .cpp for the raw 2x int32LE
    // values found and the still-unconfirmed field-layout/scale question.
    void parseWRLD_HORZ(uint8_t *data, size_t size);
    void parseWRLD_WTCH(uint8_t *data, size_t size);
    void parseWRLD_PALT(uint8_t *data, size_t size);
    void parseWRLD_TERA(uint8_t *data, size_t size);
    void parseWRLD_SKYS(uint8_t *data, size_t size);
    void parseWRLD_STAR(uint8_t *data, size_t size);
    void parseWRLD_BACK(uint8_t *data, size_t size);
    void parseWRLD_GLNT(uint8_t *data, size_t size);
    void parseWRLD_SMOK(uint8_t *data, size_t size);
    void parseWRLD_LGHT(uint8_t *data, size_t size);
    void parseWRLD_ASTR(uint8_t *data, size_t size);
    // DUST is confirmed byte-for-byte IDENTICAL across every one of the 43
    // real WORLD.IFF-family files that carry it — a fixed engine-wide
    // default, not real per-mission data. Still worth extracting for real
    // (unlike PLAQ/HOVR/VPRT/DOTS below): this drives the grey motion-dust
    // particles that spawn near the player in open space and drift
    // backward as they fly (confirmed against live gameplay) — see the
    // dustCount/dustSpawnRadius field comments.
    void parseWRLD_DUST(uint8_t *data, size_t size);
    // PLAQ/HOVR/VPRT/DOTS: confirmed byte-for-byte IDENTICAL across every
    // one of the 43 real WORLD.IFF-family files in missions.tre that carry
    // them (cross-compared directly) — fixed engine-wide defaults, not
    // real per-mission data. Registered (rather than left for IFFSaxLexer
    // to silently skip as unknown) so that's documented in code instead of
    // just in session notes; intentionally still no-op parses since
    // there's no per-mission value to extract. PLAQ specifically mirrors
    // PALT: a count(2) + the literal ASCII name "plaques\0", a reference
    // to a shared resource rather than embedded data. VPRT (16 bytes)
    // decodes cleanly as 4x uint16 (each embedded in a 4-byte slot, upper
    // 16 bits always 0 — same packing SCCockpit's own COCK>VPRT/VDU use):
    // (0,0)-(319,199) then (0,0)-(639,479) — the VGA and SVGA full-screen
    // viewport bounds, always full-frame here (unlike the cockpit's own
    // per-view VPRT, which crops around the instrument panel) since WRLD
    // has no cockpit overlay of its own to crop around. HOVR/DOTS/WTCH/LGHT
    // remain genuinely undecoded — see their own parse functions' comments
    // in the .cpp for the raw values found and unconfirmed leads. HORZ
    // (declared above, not part of this group) is user-confirmed to be
    // horizon data, but its exact field layout is still unconfirmed — see
    // its own parse function's comment.
    void parseWRLD_PLAQ(uint8_t *data, size_t size);
    void parseWRLD_HOVR(uint8_t *data, size_t size);
    void parseWRLD_VPRT(uint8_t *data, size_t size);
    void parseWRLD_DOTS(uint8_t *data, size_t size);
    void parseWRLD_CAMR(uint8_t *data, size_t size);
    // Real sub-chunk names inside WRLD>CAMR, confirmed by walking the raw
    // IFF tree directly (byte-verified across all 43 real WORLD.IFF-family
    // files) — this previously named a DIFFERENT, made-up set (STRT/CKPT/
    // VICT/COMP) that doesn't exist anywhere in the real data, and
    // parseWRLD_CAMR itself never even set up a sub-lexer to reach them,
    // so none of it was ever actually parsed. CHAS/ROTA/TARG/WEAP were
    // right; the rest (AUTO/CAMR/COCK/NAV/TRAK/VCTC) are newly identified.
    // NAV is null-padded on disk ("NAV\0") — see its registration below.
    // User-confirmed (2026-07 session) semantic meaning + real WC3 key
    // binding per chunk: TARG=Target, ROTA=Rotate, CAMR=Camera (generic/
    // self), CHAS=Chase Camera (F5), NAV=Navmap (N), COCK=Cockpit,
    // VCTC=Victim Camera (F9), WEAP=Weapon Camera (F8), TRAK=Track Camera
    // (F10), AUTO=Autopilot Camera (A). None of these real WC3 function-key
    // bindings exist in the engine yet — SCStrike.cpp's camera_mode/View
    // system (View::FRONT/FOLLOW/RIGHT/LEFT/REAR/TARGET/EYE_ON_TARGET/
    // MISSILE_CAM/etc.) is Strike Commander's own legacy view-cycling key
    // scheme, not this one; wiring F5/F8/F9/F10/A to real WC3 camera modes
    // (and consuming these chunks' per-mode FOV/position data instead of
    // leaving it parsed-but-unused) is still an open gap, not done here.
    // AUTO is a REPEATED chunk (4 separate AUTO records seen in one CAMR
    // FORM, sizes 77-98, not one chunk whose size differs per file) and
    // contains readable embedded strings — "TAKEOFF", "PLAYER", "COCKPIT"
    // seen so far — clearly one autopilot/camera-mode sequence label per
    // instance, but the full field layout isn't decoded yet. All still
    // no-op stubs; only the dispatch (previously dead) is fixed here.
    //
    // Partial decode of one real WORLD.IFF's CAMR (session notes, not
    // stored anywhere — nothing renders this yet): TARG/ROTA/CHAS/COCK/
    // VCTC/WEAP/TRAK all share a common record shape: an 8-10 byte
    // null-padded ASCII mode label matching the chunk's own name (e.g.
    // "TARGET\0\0\0\0", "COCKPIT\0"), an 8-byte null-padded actor name
    // (always "PLAYER\0\0" in this sample), then a shared 10-byte tail:
    // int32(50000) + int16(a per-mode value — 40 for TARG/ROTA/CHAS/VCTC/
    // WEAP/TRAK, 30 for COCK; plausibly an FOV in degrees, narrower for the
    // cockpit view) + int16(2560) + int16(0). VCTC/WEAP have extra trailing
    // fields beyond this shared tail, not yet examined.
    //
    // AUTO records share this same header (label + "PLAYER" + the
    // int32(50000)/int16 tail) but carry substantially more trailing data
    // (98 bytes for TAKEOFF/LANDING vs. 28 for the simple types) — user-
    // suggested (2026-07 session) these are the takeoff/landing/autopilot
    // camera positions, which fits: byte-diffing the real TAKEOFF vs
    // LANDING records shows they're identical through their shared header
    // and only start diverging around byte offset 36 (several int32/int16
    // fields differ from there on — some look plausibly like signed
    // position/orientation components, e.g. offsets 76/80 in TAKEOFF are
    // exact negatives of each other, though that pattern doesn't hold for
    // LANDING). Exact field boundaries/meaning past offset 36 aren't
    // confirmed — would need more real WORLD.IFF samples (ideally ones
    // where TAKEOFF/LANDING camera placement visibly differs) to pin down
    // which fields are position vs. orientation vs. timing.
    //
    // Now parsed for real into WorldCameraAutoSequence/cameraAutoSequences
    // below — the confirmed header fields are extracted, the rest is kept
    // as a raw byte blob rather than guessing a split. The takeoff/landing
    // autopilot gameplay feature (SCStrike) deliberately does NOT consume
    // that raw blob yet: its camera path is driven procedurally from live
    // game state (the carrier's own transform) instead, per a real user-
    // observed shot description, so this data stays "parsed but unused"
    // (same intermediate state as WorldAsteroidField) until a confident
    // decode of the trailing bytes exists.
    void parseWRLD_CAMR_AUTO(uint8_t *data, size_t size);
    void parseWRLD_CAMR_CAMR(uint8_t *data, size_t size);
    void parseWRLD_CAMR_CHAS(uint8_t *data, size_t size);
    void parseWRLD_CAMR_COCK(uint8_t *data, size_t size);
    void parseWRLD_CAMR_NAV(uint8_t *data, size_t size);
    void parseWRLD_CAMR_ROTA(uint8_t *data, size_t size);
    void parseWRLD_CAMR_TARG(uint8_t *data, size_t size);
    void parseWRLD_CAMR_TRAK(uint8_t *data, size_t size);
    void parseWRLD_CAMR_VCTC(uint8_t *data, size_t size);
    void parseWRLD_CAMR_WEAP(uint8_t *data, size_t size);

public:
    std::string tera;
    std::vector<WorldSkyFace> skyFaces;
    std::vector<WorldAsteroidField> asteroidFields;
    std::vector<WorldCameraAutoSequence> cameraAutoSequences;
    // WRLD>INFO's 3 int32LE fields. infoField1 correlates exactly with
    // whether this world is a GROUND mission: the 5 worlds with no DUST
    // chunk (ALCOR, KILRAH, REACTOR, TSTPLNT, WORLDT) read (2000, 66286)
    // instead of the normal (2000, ~32.7-32.9 million) — and those are
    // exactly the 5 worlds (out of 43) that carry a TERA chunk (real
    // terrain filename; WORLDT even points at "kilrah", same terrain as
    // KILRAH.IFF itself). Dust/nebula rendering doesn't apply to an
    // atmospheric/ground scene, so infoField1 is plausibly a space-vs-
    // ground render-mode flag, though still not independently confirmed as
    // exactly that (vs. just always correlated with it). Re-checked against
    // a real space-mission WORLD.IFF (missions.tre) this session:
    // infoField0 read 1250, not the previously-assumed constant 2000, and
    // infoField1 read 32900846, close to but not exactly the previously-
    // assumed 32768750 — so infoField0 is NOT a fixed constant across every
    // space world as earlier notes claimed, and infoField1's "space" value
    // isn't perfectly fixed either (revised to "~32.7-32.9 million" above);
    // more real samples would be needed to know whether infoField0/1 are
    // genuine per-mission data. worldRadius is 250000 in every sample
    // except WRLDM2 (350000, a mission with a larger play space) —
    // plausible view/world-bounds radius, matches the scale of
    // max_view_distance elsewhere.
    int32_t infoField0{0};
    int32_t infoField1{0};
    int32_t worldRadius{0};
    // STAR's own "1.10"-format shape entry: 2-3 progressively smaller
    // diamond/glint sprites (reusing the same RLE codec as WC3's gump
    // sprites and cockpit art — see RSWC3Shape.h) used to draw twinkling
    // stars instead of flat single-pixel dots.
    RSImageSet starSprites;
    int32_t starCount{0};
    // WRLD's own BACK chunk: a single byte, an index into the main VGA
    // palette (DATA\PALETTE\PALETTE.IFF — PALT's own 8-byte body is just the
    // literal ASCII name "palette\0", a reference to that same shared
    // palette rather than embedded color data) giving the space background
    // clear color. Confirmed against WORLD.IFF: index 0x11 (17) resolves to
    // a dark navy blue, not black.
    uint8_t backgroundColorIndex{0};
    // WRLD>DUST's 6 int32LE fields: (50, 16777216, 0, 65536000, 1573055,
    // 78644200) in every sample — identical everywhere, so treated as a
    // fixed default rather than truly per-mission. Several divide cleanly
    // by 65536 (this format's usual 16.16 fixed-point convention):
    // field[0]=50 (count), field[1]/65536=256.0 exactly (spawn radius),
    // field[3]/65536=1000.0 exactly (plausibly a despawn distance or
    // speed). field[2] is always 0 (unused). field[4]/field[5] don't
    // divide as cleanly (~24.0 and ~1200.06) and aren't confidently
    // decoded — not stored. hasDust is false for missions with no DUST
    // chunk at all (the 5 ground-mission worlds — see infoField1's own
    // comment), which should mean no dust effect for those.
    bool hasDust{false};
    int32_t dustCount{50};
    float dustSpawnRadius{256.0f};
    RSWorld();
    ~RSWorld();
    void InitFromRAM(uint8_t *data, size_t size);
};
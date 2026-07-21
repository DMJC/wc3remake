#include "RSWorld.h"
#include "RSWC3Shape.h"

RSWorld::RSWorld() {

}
RSWorld::~RSWorld() {

}
void RSWorld::InitFromRAM(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;

    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["WRLD"] = std::bind(&RSWorld::parseWRLD, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}

void RSWorld::parseWRLD(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;

    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["INFO"] = std::bind(&RSWorld::parseWRLD_INFO, this, std::placeholders::_1, std::placeholders::_2);

    handlers["HORZ"] = std::bind(&RSWorld::parseWRLD_HORZ, this, std::placeholders::_1, std::placeholders::_2);
    handlers["WTCH"] = std::bind(&RSWorld::parseWRLD_WTCH, this, std::placeholders::_1, std::placeholders::_2);
    handlers["PALT"] = std::bind(&RSWorld::parseWRLD_PALT, this, std::placeholders::_1, std::placeholders::_2);
    handlers["TERA"] = std::bind(&RSWorld::parseWRLD_TERA, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SKYS"] = std::bind(&RSWorld::parseWRLD_SKYS, this, std::placeholders::_1, std::placeholders::_2);
    handlers["STAR"] = std::bind(&RSWorld::parseWRLD_STAR, this, std::placeholders::_1, std::placeholders::_2);
    handlers["GLNT"] = std::bind(&RSWorld::parseWRLD_GLNT, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SMOK"] = std::bind(&RSWorld::parseWRLD_SMOK, this, std::placeholders::_1, std::placeholders::_2);
    handlers["LGHT"] = std::bind(&RSWorld::parseWRLD_LGHT, this, std::placeholders::_1, std::placeholders::_2);
    handlers["ASTR"] = std::bind(&RSWorld::parseWRLD_ASTR, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DUST"] = std::bind(&RSWorld::parseWRLD_DUST, this, std::placeholders::_1, std::placeholders::_2);
    handlers["PLAQ"] = std::bind(&RSWorld::parseWRLD_PLAQ, this, std::placeholders::_1, std::placeholders::_2);
    handlers["HOVR"] = std::bind(&RSWorld::parseWRLD_HOVR, this, std::placeholders::_1, std::placeholders::_2);
    handlers["VPRT"] = std::bind(&RSWorld::parseWRLD_VPRT, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DOTS"] = std::bind(&RSWorld::parseWRLD_DOTS, this, std::placeholders::_1, std::placeholders::_2);
    handlers["CAMR"] = std::bind(&RSWorld::parseWRLD_CAMR, this, std::placeholders::_1, std::placeholders::_2);
    handlers["BACK"] = std::bind(&RSWorld::parseWRLD_BACK, this, std::placeholders::_1, std::placeholders::_2);

    lexer.InitFromRAM(data, size, handlers);
}

// 3x int32LE — see the infoField0/infoField1/worldRadius field comments in
// RSWorld.h for what's known about each.
void RSWorld::parseWRLD_INFO(uint8_t *data, size_t size) {
    if (size < 12) {
        return;
    }
    ByteStream stream(data, size);
    this->infoField0 = stream.ReadInt32LE();
    this->infoField1 = stream.ReadInt32LE();
    this->worldRadius = stream.ReadInt32LE();
}

// User-confirmed (2026-07 session): this is horizon data. 8 bytes, real
// WORLD.IFF sample: 2x int32LE = (2557952, 1279488), the first almost
// exactly 2x the second (ratio 1.9998) — plausibly a far/near or fade-
// end/fade-start distance pair, matching this engine's own ground-fog
// idiom elsewhere (SCRenderer::renderWorldSolid's GL_FOG_START/GL_FOG_END,
// also a 2-value linear falloff), though which field is which isn't
// confirmed, and neither value divides cleanly by 65536 (this format's
// usual 16.16 fixed-point scale) or any other obvious unit — so the
// concept is confirmed, the exact field layout/scale still isn't.
void RSWorld::parseWRLD_HORZ(uint8_t *data, size_t size) {}

// 2 bytes, real WORLD.IFF sample: 1x uint16LE = 500. "WTCH" (watch) reads
// like a timer/countdown value but this is a single sample, no cross-file
// comparison done — not confirmed.
void RSWorld::parseWRLD_WTCH(uint8_t *data, size_t size) {}

void RSWorld::parseWRLD_PALT(uint8_t *data, size_t size) {}

void RSWorld::parseWRLD_DUST(uint8_t *data, size_t size) {
    if (size < 16) {
        return;
    }
    ByteStream stream(data, size);
    this->hasDust = true;
    this->dustCount = stream.ReadInt32LE();
    this->dustSpawnRadius = (float)stream.ReadInt32LE() / 65536.0f;
}

void RSWorld::parseWRLD_PLAQ(uint8_t *data, size_t size) {}

// 4 bytes, real WORLD.IFF sample: 1x int32LE = 256000. "HOVR" (hover) —
// no confirmed meaning; doesn't divide cleanly by 65536, does divide
// cleanly by 1000 (=256), unclear if that's significant or coincidence.
void RSWorld::parseWRLD_HOVR(uint8_t *data, size_t size) {}

// See RSWorld.h's own comment above (next to the PLAQ/HOVR/VPRT/DOTS
// declaration group) for the confirmed 4x-uint16 VGA/SVGA viewport decode.
void RSWorld::parseWRLD_VPRT(uint8_t *data, size_t size) {}

// 1 byte, real WORLD.IFF sample: 175 (0xaf). "DOTS" — no confirmed
// meaning. Coincidentally the exact same value as WorldAsteroidField's own
// "unknown1" field (also always 175 in every ASTR record) — worth noting
// in case they share a meaning (e.g. both some kind of fixed magic/version
// byte), but this is one data point, not independently confirmed.
void RSWorld::parseWRLD_DOTS(uint8_t *data, size_t size) {}

// See WorldAsteroidField's own comment (RSWorld.h) for the record layout.
void RSWorld::parseWRLD_ASTR(uint8_t *data, size_t size) {
    constexpr size_t kRecord = 108;
    ByteStream stream(data, size);
    if (size < 4) {
        return;
    }
    uint32_t count = stream.ReadUInt32LE();
    for (uint32_t i = 0; i < count && stream.GetSize() - stream.GetCurrentPosition() >= kRecord; i++) {
        WorldAsteroidField field;
        field.count = stream.ReadInt32LE();
        field.variantCount = stream.ReadInt32LE();
        stream.ReadInt32LE(); // reserved, always 0
        stream.ReadInt32LE(); // reserved, always 0
        field.centerX = stream.ReadInt32LE();
        field.centerY = stream.ReadInt32LE();
        field.centerZ = stream.ReadInt32LE();
        for (int t = 0; t < 3; t++) {
            field.scatterVolume[t][0] = stream.ReadInt32LE();
            field.scatterVolume[t][1] = stream.ReadInt32LE();
            field.scatterVolume[t][2] = stream.ReadInt32LE();
        }
        field.unknown1 = stream.ReadInt32LE();
        field.spread = stream.ReadInt32LE();
        field.unknown2 = stream.ReadInt32LE();
        field.axisParam[0] = stream.ReadInt32LE();
        field.axisParam[1] = stream.ReadInt32LE();
        field.axisParam[2] = stream.ReadInt32LE();
        field.unknown3 = stream.ReadInt32LE();
        field.modelNameRaw = stream.ReadString(16);
        this->asteroidFields.push_back(field);
    }
}

void RSWorld::parseWRLD_BACK(uint8_t *data, size_t size) {
    if (size < 1) {
        return;
    }
    this->backgroundColorIndex = data[0];
}

void RSWorld::parseWRLD_TERA(uint8_t *data, size_t size) {IFFSaxLexer lexer;

    ByteStream stream(data, size);
    this->tera = stream.ReadStringNoSize(size);
}

// 24-byte records: name(8, null-terminated) + dirX/dirY/dirZ/distance
// (int32LE each) — one per skybox face. Reverse-engineered empirically
// against WORLD.IFF: 6 records, directions exactly matching the 6 faces of
// a cube ((-1,0,0),(1,0,0),(0,1,0),(0,-1,0),(0,0,-1),(0,0,1)), each naming
// a DATA\OBJECTS\<NAME>.IFF distant-billboard entity (OBJT>DIST/APPR>DFLT).
void RSWorld::parseWRLD_SKYS(uint8_t *data, size_t size) {
    constexpr size_t kRecord = 24;
    ByteStream stream(data, size);
    for (size_t pos = 0; pos + kRecord <= size; pos += kRecord) {
        WorldSkyFace face;
        face.name = stream.ReadString(8);
        face.dirX = stream.ReadInt32LE();
        face.dirY = stream.ReadInt32LE();
        face.dirZ = stream.ReadInt32LE();
        face.distance = stream.ReadInt32LE();
        this->skyFaces.push_back(face);
    }
}

// STAR's own body: a 24-byte header (6x int32LE, meaning not fully
// identified — likely density/brightness/twinkle-rate parameters) followed
// immediately by one WC3 "1.1x"-format shape-pak entry (see RSWC3Shape.h)
// holding 2-3 progressively smaller diamond/glint sprites (confirmed against
// WORLD.IFF: 7x7, 5x5, 4x4). The very first header field lines up with the
// sprite count actually used elsewhere (e.g. 20 in WORLD.IFF) closely enough
// to be a plausible star-count parameter, but isn't independently confirmed.
void RSWorld::parseWRLD_STAR(uint8_t *data, size_t size) {
    constexpr size_t kHeader = 24;
    if (size <= kHeader) {
        return;
    }
    ByteStream stream(data, size);
    this->starCount = stream.ReadInt32LE();

    RSImageSet *img = RSWC3DecodeShapeEntry(data + kHeader, size - kHeader);
    this->starSprites = *img;
    delete img;
}

void RSWorld::parseWRLD_GLNT(uint8_t *data, size_t size) {}

void RSWorld::parseWRLD_SMOK(uint8_t *data, size_t size) {}

// 16 bytes, real WORLD.IFF sample: as 8x int16LE = [3, 100, 0, 5, -10, -25,
// 100, 1]. "LGHT" (light) — plausibly type/mode=3, intensity=100, a
// direction-ish pair (-10,-25), range=100, flag=1, but this is one sample
// with no cross-file comparison to confirm which fields are constant vs.
// real per-mission data — not confirmed.
void RSWorld::parseWRLD_LGHT(uint8_t *data, size_t size) {}

// See the sub-handler declarations in RSWorld.h for how these real names
// were confirmed (and what the old STRT/CKPT/VICT/COMP guesses got wrong).
// This previously did nothing at all — no sub-lexer, so none of CAMR's
// content was ever reached regardless of handler names.
void RSWorld::parseWRLD_CAMR(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["AUTO"] = std::bind(&RSWorld::parseWRLD_CAMR_AUTO, this, std::placeholders::_1, std::placeholders::_2);
    handlers["CAMR"] = std::bind(&RSWorld::parseWRLD_CAMR_CAMR, this, std::placeholders::_1, std::placeholders::_2);
    handlers["CHAS"] = std::bind(&RSWorld::parseWRLD_CAMR_CHAS, this, std::placeholders::_1, std::placeholders::_2);
    handlers["COCK"] = std::bind(&RSWorld::parseWRLD_CAMR_COCK, this, std::placeholders::_1, std::placeholders::_2);
    // Null-padded on disk ("NAV\0", not "NAV "/"NAV") — IFFSaxLexer looks up
    // handlers by the full 4-byte tag including any embedded null, so this
    // needs the explicit-length string constructor (see the identical
    // MISN>NAV gotcha fixed earlier this session in RSMission.cpp/
    // RSCockpit.cpp) or the plain 3-char literal key would silently never
    // match and this handler would never fire.
    handlers[std::string("NAV", 4)] = std::bind(&RSWorld::parseWRLD_CAMR_NAV, this, std::placeholders::_1, std::placeholders::_2);
    handlers["ROTA"] = std::bind(&RSWorld::parseWRLD_CAMR_ROTA, this, std::placeholders::_1, std::placeholders::_2);
    handlers["TARG"] = std::bind(&RSWorld::parseWRLD_CAMR_TARG, this, std::placeholders::_1, std::placeholders::_2);
    handlers["TRAK"] = std::bind(&RSWorld::parseWRLD_CAMR_TRAK, this, std::placeholders::_1, std::placeholders::_2);
    handlers["VCTC"] = std::bind(&RSWorld::parseWRLD_CAMR_VCTC, this, std::placeholders::_1, std::placeholders::_2);
    handlers["WEAP"] = std::bind(&RSWorld::parseWRLD_CAMR_WEAP, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}

// The 8-byte mode label is confident and uniform across all 4 real records
// ("TAKEOFF\0"/"LANDING\0"/"AUTOPILT"/"AUTOCAPS" — the latter two exactly
// fill 8 bytes with no null needed). Past that, AUTO's real byte layout
// does NOT cleanly match the label+actorName+shared-tail order the other
// CAMR children (TARG/ROTA/etc.) use — the "PLAYER" actor name and the
// shared int32(50000)/int16 tail pattern both appear somewhere in the
// remainder, but not at a single fixed, confidently-boundaried offset
// across all 4 records. Rather than guess a split (see WorldCameraAutoSequence's
// own comment), everything past the label is kept as one raw blob.
void RSWorld::parseWRLD_CAMR_AUTO(uint8_t *data, size_t size) {
    constexpr size_t kLabelSize = 8;
    if (size < kLabelSize) {
        return;
    }
    ByteStream stream(data, size);
    WorldCameraAutoSequence seq;
    seq.modeLabel = stream.ReadString(kLabelSize);
    seq.tailRaw = stream.ReadBytes(size - kLabelSize);
    this->cameraAutoSequences.push_back(seq);
}

// Shared record shape for TARG/ROTA/COCK/NAV/VCTC/WEAP/TRAK — see
// WorldCameraModeParams's own comment for the confirmed field breakdown.
// CAMR/CHAS don't use this (see their own parse functions below) since
// they carry extra undecoded bytes BEFORE the actor name instead of just
// the plain 2-byte modeFlags gap this handles.
static WorldCameraModeParams ParseSimpleWorldCameraModeParams(uint8_t *data, size_t size) {
    WorldCameraModeParams p;
    constexpr size_t kHeaderSize = 28; // label(8) + modeFlags(2) + actorName(8) + shared tail(10)
    if (size < kHeaderSize) {
        return p;
    }
    ByteStream stream(data, size);
    p.modeLabel = stream.ReadString(8);
    p.modeFlags = stream.ReadShort();
    p.actorName = stream.ReadString(8);
    p.sharedField0 = stream.ReadInt32LE();
    p.fovDegrees = stream.ReadShort();
    p.sharedField1 = stream.ReadShort();
    p.sharedField2 = stream.ReadShort();
    if (size > kHeaderSize) {
        p.extraRaw = stream.ReadBytes(size - kHeaderSize);
    }
    return p;
}
void RSWorld::parseWRLD_CAMR_CAMR(uint8_t *data, size_t size) {
    // No actor-name field — a free/self camera, not locked to a target.
    // label(8) + preActorRaw(20, undecoded) + shared tail(10) = 38 bytes,
    // matching every real sample's chunk size exactly.
    constexpr size_t kPreActorSize = 20;
    if (size < 8 + kPreActorSize + 10) {
        return;
    }
    ByteStream stream(data, size);
    WorldCameraModeParams p;
    p.modeLabel = stream.ReadString(8);
    p.preActorRaw = stream.ReadBytes(kPreActorSize);
    p.sharedField0 = stream.ReadInt32LE();
    p.fovDegrees = stream.ReadShort();
    p.sharedField1 = stream.ReadShort();
    p.sharedField2 = stream.ReadShort();
    this->camCamera = p;
}

void RSWorld::parseWRLD_CAMR_CHAS(uint8_t *data, size_t size) {
    // label(8) + preActorRaw(14, undecoded — plausibly a chase-camera
    // position/offset vector, not confidently split yet) + actorName(8) +
    // shared tail(10) = 40 bytes, matching every real sample exactly.
    constexpr size_t kPreActorSize = 14;
    if (size < 8 + kPreActorSize + 8 + 10) {
        return;
    }
    ByteStream stream(data, size);
    WorldCameraModeParams p;
    p.modeLabel = stream.ReadString(8);
    p.preActorRaw = stream.ReadBytes(kPreActorSize);
    p.actorName = stream.ReadString(8);
    p.sharedField0 = stream.ReadInt32LE();
    p.fovDegrees = stream.ReadShort();
    p.sharedField1 = stream.ReadShort();
    p.sharedField2 = stream.ReadShort();
    this->camChase = p;
}

void RSWorld::parseWRLD_CAMR_COCK(uint8_t *data, size_t size) { this->camCockpit = ParseSimpleWorldCameraModeParams(data, size); }

void RSWorld::parseWRLD_CAMR_NAV(uint8_t *data, size_t size) { this->camNavmap = ParseSimpleWorldCameraModeParams(data, size); }

void RSWorld::parseWRLD_CAMR_ROTA(uint8_t *data, size_t size) { this->camRotate = ParseSimpleWorldCameraModeParams(data, size); }

void RSWorld::parseWRLD_CAMR_TARG(uint8_t *data, size_t size) { this->camTarget = ParseSimpleWorldCameraModeParams(data, size); }

void RSWorld::parseWRLD_CAMR_TRAK(uint8_t *data, size_t size) { this->camTrack = ParseSimpleWorldCameraModeParams(data, size); }

void RSWorld::parseWRLD_CAMR_VCTC(uint8_t *data, size_t size) { this->camVictim = ParseSimpleWorldCameraModeParams(data, size); }

void RSWorld::parseWRLD_CAMR_WEAP(uint8_t *data, size_t size) { this->camWeapon = ParseSimpleWorldCameraModeParams(data, size); }

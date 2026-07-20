#include "precomp.h"
#include "RSCockpit.h"
#include "RSWC3Shape.h"

RSCockpit::RSCockpit() {
    this->asset_manager = &AssetManager::getInstance();
}
RSCockpit::~RSCockpit(){

}

/**
 * \brief Initialize the cockpit object from a block of memory
 *
 * Given a block of memory, parse it into an RSCockpit object. The
 * format of the data is assumed to be an IFF CKPT chunk.
 *
 * \param data Pointer to the memory block to parse
 * \param size The size of the block of memory to parse
 */
void RSCockpit::InitFromRam(uint8_t* data, size_t size) {
    IFFSaxLexer lexer;

	std::unordered_map<std::string, std::function<void(uint8_t* data, size_t size)>> handlers;
	handlers["CKPT"] = std::bind(&RSCockpit::parseCKPT, this, std::placeholders::_1, std::placeholders::_2);
	
	lexer.InitFromRAM(data, size, handlers);
    TreEntry *plaqu_entry = this->asset_manager->GetEntryByName("..\\..\\DATA\\COCKPITS\\PLAQUES.IFF");
    if (plaqu_entry) {
        PLAQ.InitFromRAM(plaqu_entry->data, plaqu_entry->size);
    }
}

/**
 * \brief Parse an IFF CKPT chunk
 *
 * Parse an IFF CKPT chunk, which contains all the cockpit data
 *
 * \param data Pointer to the memory block to parse
 * \param size The size of the block of memory to parse
 */
void RSCockpit::parseCKPT(uint8_t* data, size_t size) {
    IFFSaxLexer lexer;

	std::unordered_map<std::string, std::function<void(uint8_t* data, size_t size)>> handlers;
	handlers["INFO"] = std::bind(&RSCockpit::parseINFO, this, std::placeholders::_1, std::placeholders::_2);
	handlers["ARTP"] = std::bind(&RSCockpit::parseARTP, this, std::placeholders::_1, std::placeholders::_2);
	handlers["VTMP"] = std::bind(&RSCockpit::parseVTMP, this, std::placeholders::_1, std::placeholders::_2);
    handlers["EJEC"] = std::bind(&RSCockpit::parseEJEC, this, std::placeholders::_1, std::placeholders::_2);
	handlers["GUNF"] = std::bind(&RSCockpit::parseGUNF, this, std::placeholders::_1, std::placeholders::_2);
	handlers["GHUD"] = std::bind(&RSCockpit::parseGHUD, this, std::placeholders::_1, std::placeholders::_2);
    handlers["REAL"] = std::bind(&RSCockpit::parseREAL, this, std::placeholders::_1, std::placeholders::_2);
    handlers["CHUD"] = std::bind(&RSCockpit::parseCHUD, this, std::placeholders::_1, std::placeholders::_2);
    handlers["MONI"] = std::bind(&RSCockpit::parseMONI, this, std::placeholders::_1, std::placeholders::_2);
    handlers["INST"] = std::bind(&RSCockpit::parseMONI_INST, this, std::placeholders::_1, std::placeholders::_2);
    handlers["FADE"] = std::bind(&RSCockpit::parseFADE, this, std::placeholders::_1, std::placeholders::_2);

	lexer.InitFromRAM(data, size, handlers);
}

void RSCockpit::InitFromWC3Ram(uint8_t* data, size_t size) {
    IFFSaxLexer lexer;

    std::unordered_map<std::string, std::function<void(uint8_t* data, size_t size)>> handlers;
    handlers["COCK"] = std::bind(&RSCockpit::parseWC3_COCK, this, std::placeholders::_1, std::placeholders::_2);

    lexer.InitFromRAM(data, size, handlers);
}

void RSCockpit::parseWC3_COCK(uint8_t* data, size_t size) {
    IFFSaxLexer lexer;

    std::unordered_map<std::string, std::function<void(uint8_t* data, size_t size)>> handlers;
    handlers["SHAP"] = std::bind(&RSCockpit::parseWC3_COCK_SHAP, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DETH"] = std::bind(&RSCockpit::parseWC3_COCK_DETH, this, std::placeholders::_1, std::placeholders::_2);
    // The raw 4-byte tag is "VGA" + a NULL padding byte, not a space —
    // confirmed by decoding the raw chunk bytes directly (easy to miss:
    // terminal/Python-repr output of a 3-letter tag looks identical for
    // both, and IFFSaxLexer's own chunk_stype key preserves whichever
    // byte is actually there — see its own comment on why chunk_stype
    // must be looked up by full 4-char content, not a C-string). A plain
    // `handlers["VGA "]` (space) is a DIFFERENT, non-matching std::string
    // key from the real "VGA\0" tag and silently never fires — which is
    // exactly what happened here: this handler, and therefore the whole
    // targetHudVGA/SVGA real-bounding-box fix in
    // parseWC3_COCK_RES_FRNT_TARG, was dead code until this was caught.
    // "SVGA" needs no such fix (already exactly 4 non-null characters).
    handlers[std::string("VGA", 4)] = [this](uint8_t* d, size_t s) { this->parseWC3_COCK_RES(d, s, false); };
    handlers["SVGA"] = [this](uint8_t* d, size_t s) { this->parseWC3_COCK_RES(d, s, true); };

    lexer.InitFromRAM(data, size, handlers);
}
void RSCockpit::parseWC3_COCK_RES(uint8_t* data, size_t size, bool isSVGA) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t* data, size_t size)>> handlers;
    // FRNT (normal cockpit view) and HUD (cockpit frame off, gauges/MFDs
    // only — see SCCockpit::WC3CockpitViewMode::HUD_ONLY) are both decoded;
    // LEFT/RGHT/BACK are left to IFFSaxLexer's default skip.
    handlers["FRNT"] = [this, isSVGA](uint8_t* d, size_t s) { this->parseWC3_COCK_RES_FRNT(d, s, isSVGA); };
    // HUD\0 tag (3 chars + null byte) — same null-byte trick as "VGA\0"/"VDU\0".
    handlers[std::string("HUD", 4)] = [this, isSVGA](uint8_t* d, size_t s) { this->parseWC3_COCK_RES_HUD(d, s, isSVGA); };
    lexer.InitFromRAM(data, size, handlers);
}
void RSCockpit::parseWC3_COCK_RES_FRNT(uint8_t* data, size_t size, bool isSVGA) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t* data, size_t size)>> handlers;
    handlers["TARG"] = [this, isSVGA](uint8_t* d, size_t s) { this->parseWC3_COCK_RES_FRNT_TARG(d, s, isSVGA); };
    handlers["INST"] = [this, isSVGA](uint8_t* d, size_t s) { this->parseWC3_COCK_RES_FRNT_INST(d, s, isSVGA, false); };
    // VDU\0 tag (3 chars + null byte) — same null-byte trick as "VGA\0" above.
    handlers[std::string("VDU", 4)] = [this, isSVGA](uint8_t* d, size_t s) { this->parseWC3_COCK_RES_FRNT_VDU(d, s, isSVGA, false); };
    handlers["TEXT"] = [this, isSVGA](uint8_t* d, size_t s) { this->parseWC3_COCK_RES_FRNT_TEXT(d, s, isSVGA, false); };
    handlers["WEAP"] = [this, isSVGA](uint8_t* d, size_t s) { this->parseWC3_COCK_RES_FRNT_WEAP(d, s, isSVGA, false); };
    handlers[std::string("SYS", 4)] = [this, isSVGA](uint8_t* d, size_t s) { this->parseWC3_COCK_RES_FRNT_SYS(d, s, isSVGA, false); };
    handlers["DAMG"] = [this, isSVGA](uint8_t* d, size_t s) { this->parseWC3_COCK_RES_FRNT_DAMG(d, s, isSVGA, false); };
    lexer.InitFromRAM(data, size, handlers);
}
// HUD>TARG is deliberately not registered here: its box is (-1,-1,-1,-1) on
// every file checked (an "unbounded, no clipping" sentinel for the
// always-on HUD overlay, as opposed to FRNT's real boresight-region box),
// so there's nothing useful to extract from it.
void RSCockpit::parseWC3_COCK_RES_HUD(uint8_t* data, size_t size, bool isSVGA) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t* data, size_t size)>> handlers;
    handlers["INST"] = [this, isSVGA](uint8_t* d, size_t s) { this->parseWC3_COCK_RES_FRNT_INST(d, s, isSVGA, true); };
    handlers[std::string("VDU", 4)] = [this, isSVGA](uint8_t* d, size_t s) { this->parseWC3_COCK_RES_FRNT_VDU(d, s, isSVGA, true); };
    handlers["TEXT"] = [this, isSVGA](uint8_t* d, size_t s) { this->parseWC3_COCK_RES_FRNT_TEXT(d, s, isSVGA, true); };
    handlers["WEAP"] = [this, isSVGA](uint8_t* d, size_t s) { this->parseWC3_COCK_RES_FRNT_WEAP(d, s, isSVGA, true); };
    handlers[std::string("SYS", 4)] = [this, isSVGA](uint8_t* d, size_t s) { this->parseWC3_COCK_RES_FRNT_SYS(d, s, isSVGA, true); };
    handlers["DAMG"] = [this, isSVGA](uint8_t* d, size_t s) { this->parseWC3_COCK_RES_FRNT_DAMG(d, s, isSVGA, true); };
    lexer.InitFromRAM(data, size, handlers);
}
void RSCockpit::parseWC3_COCK_RES_FRNT_TARG(uint8_t* data, size_t size, bool isSVGA) {
    if (size < 24) {
        return;
    }
    ByteStream bs(data, size);
    TargetHudBox box;
    box.xMin = bs.ReadInt32LE();
    box.yMin = bs.ReadInt32LE();
    box.xMax = bs.ReadInt32LE();
    box.yMax = bs.ReadInt32LE();
    box.primaryShapeId = bs.ReadUInt32LE();
    box.secondaryShapeId = bs.ReadUInt32LE();
    box.valid = (box.xMax > box.xMin && box.yMax > box.yMin);
    if (isSVGA) {
        this->targetHudSVGA = box;
    } else {
        this->targetHudVGA = box;
    }
}
void RSCockpit::parseWC3_COCK_RES_FRNT_INST(uint8_t* data, size_t size, bool isSVGA, bool isHud) {
    // 42-byte records confirmed by binary analysis: 1134/840/1092 bytes in
    // ARWPIT.IFF all divide evenly. Fields used here (all int16 LE):
    //   offset  0: mode word (high byte 0xff = screen-absolute overlay;
    //               other values = MFD-relative, conditional, or gauge-group)
    //   offset  2: x screen position (or MFD-relative x for non-0xff modes)
    //   offset  6: y screen position (or MFD-relative y for non-0xff modes)
    //   offset 38: shape id (-1 = disabled/skip)
    // Shared by FRNT>INST and HUD>INST (isHud picks the destination) — same
    // record format in both, real per-view data in both.
    constexpr size_t kRecordSize = 42;
    if (size == 0 || size % kRecordSize != 0) return;
    size_t count = size / kRecordSize;

    auto& vec = isHud ? (isSVGA ? this->svgaHudInstruments : this->vgaHudInstruments)
                       : (isSVGA ? this->svgaFrontInstruments : this->vgaFrontInstruments);
    vec.clear();
    vec.reserve(count);

    for (size_t i = 0; i < count; i++) {
        const uint8_t* rec = data + i * kRecordSize;
        int16_t shapeId = (int16_t)((uint16_t)rec[38] | ((uint16_t)rec[39] << 8));
        if (shapeId < 0) continue;
        int16_t x = (int16_t)((uint16_t)rec[2] | ((uint16_t)rec[3] << 8));
        int16_t y = (int16_t)((uint16_t)rec[6] | ((uint16_t)rec[7] << 8));
        uint16_t mode = (uint16_t)rec[0] | ((uint16_t)rec[1] << 8);
        vec.push_back({(uint32_t)shapeId, x, y, mode});
    }
}
void RSCockpit::parseWC3_COCK_RES_FRNT_VDU(uint8_t* data, size_t size, bool isSVGA, bool isHud) {
    // Layout (all LE int32, coordinates stored as pixel_value × 256):
    //   [0:4]   combinedShapeId  — -1 on every file checked (use left/right separately)
    //   [4:8]   leftShapeId      — SHAP pak id for left VDU background
    //   [8:12]  rightShapeId     — SHAP pak id for right VDU background
    //   [12:16] leftX  × 256
    //   [16:20] leftY  × 256
    //   [20:24] rightX × 256
    //   [24:28] rightY × 256
    //   [28:32] width  × 256
    //   [32:36] height × 256
    //   [36:87] additional bounding-box data — not decoded yet
    // Shared by FRNT>VDU and HUD>VDU (isHud picks the destination).
    if (size < 36) return;
    WC3VDULayout layout;
    auto readFP = [&](size_t offset) -> int16_t {
        uint32_t raw = (uint32_t)data[offset]
                     | ((uint32_t)data[offset+1] << 8)
                     | ((uint32_t)data[offset+2] << 16)
                     | ((uint32_t)data[offset+3] << 24);
        return (int16_t)(raw >> 8);
    };
    auto readI32 = [&](size_t offset) -> int32_t {
        return (int32_t)((uint32_t)data[offset]
                       | ((uint32_t)data[offset+1] << 8)
                       | ((uint32_t)data[offset+2] << 16)
                       | ((uint32_t)data[offset+3] << 24));
    };
    layout.combinedShapeId = readI32(0);
    layout.leftShapeId     = readI32(4);
    layout.rightShapeId    = readI32(8);
    layout.leftX           = readFP(12);
    layout.leftY           = readFP(16);
    layout.rightX          = readFP(20);
    layout.rightY          = readFP(24);
    layout.width           = readFP(28);
    layout.height          = readFP(32);
    layout.valid           = true;
    if (isHud) {
        if (isSVGA) this->svgaHudVDU = layout;
        else         this->vgaHudVDU  = layout;
    } else {
        if (isSVGA) this->svgaFrontVDU = layout;
        else         this->vgaFrontVDU  = layout;
    }
}

void RSCockpit::parseWC3_COCK_RES_FRNT_TEXT(uint8_t* data, size_t size, bool isSVGA, bool isHud) {
    // 34-byte records, no header:
    //   [0]     id   — 0x01=KPS (speed), 0x02=SET (throttle setting), others=misc
    //   [1]     mode — 0xff=screen-absolute; other values=MFD-relative (not rendered here)
    //   [2:6]   flags/unknown LE uint32
    //   [6:26]  text string, null-padded to 20 bytes (format: "KPS:%03ld" etc.)
    //   [26:30] x screen coordinate, LE int32
    //   [30:34] y screen coordinate, LE int32
    // Shared by FRNT>TEXT and HUD>TEXT (isHud picks the destination).
    const size_t kRec = 34;
    auto& dest = isHud ? (isSVGA ? svgaHudText : vgaHudText)
                        : (isSVGA ? svgaFrontText : vgaFrontText);
    dest.clear();
    size_t n = size / kRec;
    for (size_t i = 0; i < n; i++) {
        const uint8_t* r = data + i * kRec;
        WC3TextEntry e;
        e.id   = r[0];
        e.mode = r[1];
        memcpy(e.text, r + 6, 20);
        e.text[20] = '\0';
        e.x = (int32_t)(r[26] | ((uint32_t)r[27]<<8) | ((uint32_t)r[28]<<16) | ((uint32_t)r[29]<<24));
        e.y = (int32_t)(r[30] | ((uint32_t)r[31]<<8) | ((uint32_t)r[32]<<16) | ((uint32_t)r[33]<<24));
        dest.push_back(e);
    }
}

void RSCockpit::parseWC3_COCK_RES_FRNT_WEAP(uint8_t* data, size_t size, bool isSVGA, bool isHud) {
    // Flat LE int32 stream: [countA, countA*(x,y)][countB, countB*(x,y)].
    // See WC3WeaponLayout's own doc comment for the real MEDPIT/ARWPIT byte
    // layouts this was reverse-engineered from. No fixed record size (real
    // size varies per ship, 56-80 bytes observed across cockpit files) —
    // bounds-checked defensively since a malformed/truncated chunk must not
    // overrun the buffer. Shared by FRNT>WEAP and HUD>WEAP (isHud picks the
    // destination).
    auto readI32 = [&](size_t offset) -> int32_t {
        return (int32_t)((uint32_t)data[offset]
                       | ((uint32_t)data[offset+1] << 8)
                       | ((uint32_t)data[offset+2] << 16)
                       | ((uint32_t)data[offset+3] << 24));
    };
    WC3WeaponLayout layout;
    size_t pos = 0;
    auto readGroup = [&](std::vector<WC3WeaponIconAnchor>& out) -> bool {
        if (pos + 4 > size) return false;
        int32_t count = readI32(pos);
        pos += 4;
        if (count < 0 || pos + (size_t)count * 8 > size) return false;
        out.reserve((size_t)count);
        for (int32_t i = 0; i < count; i++) {
            int16_t x = (int16_t)readI32(pos);
            int16_t y = (int16_t)readI32(pos + 4);
            out.push_back({x, y});
            pos += 8;
        }
        return true;
    };
    if (readGroup(layout.groupA) && readGroup(layout.groupB)) {
        layout.valid = true;
    }
    if (isHud) {
        if (isSVGA) this->svgaHudWeap = layout;
        else        this->vgaHudWeap  = layout;
    } else {
        if (isSVGA) this->svgaFrontWeap = layout;
        else        this->vgaFrontWeap  = layout;
    }
}

void RSCockpit::parseWC3_COCK_RES_FRNT_SYS(uint8_t* data, size_t size, bool isSVGA, bool isHud) {
    // See WC3DamageSubsystemSlot's doc comment for the confirmed/unconfirmed
    // byte layout. Shared by FRNT>SYS and HUD>SYS (isHud picks the dest).
    constexpr size_t kRecordSize = 10;
    if (size == 0 || size % kRecordSize != 0) return;
    size_t count = size / kRecordSize;
    auto& dest = isHud ? (isSVGA ? this->svgaHudSys : this->vgaHudSys)
                        : (isSVGA ? this->svgaFrontSys : this->vgaFrontSys);
    dest.clear();
    dest.reserve(count);
    for (size_t i = 0; i < count; i++) {
        const uint8_t* r = data + i * kRecordSize;
        WC3DamageSubsystemSlot slot;
        slot.slotOrder = r[0];
        slot.subsystemId = r[1];
        slot.flagged = r[6] != 0;
        dest.push_back(slot);
    }
}

// FORM DAMG wraps a single real sub-chunk (TEXT — the subsystem labels);
// registered as a FORM subtype tag directly (same idiom as "VGA\0"/"SVGA"
// in parseWC3_COCK_RES), not nested under a literal "FORM" key.
void RSCockpit::parseWC3_COCK_RES_FRNT_DAMG(uint8_t* data, size_t size, bool isSVGA, bool isHud) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t* data, size_t size)>> handlers;
    handlers["TEXT"] = [this, isSVGA, isHud](uint8_t* d, size_t s) { this->parseWC3_COCK_RES_FRNT_DAMG_TEXT(d, s, isSVGA, isHud); };
    lexer.InitFromRAM(data, size, handlers);
}

void RSCockpit::parseWC3_COCK_RES_FRNT_DAMG_TEXT(uint8_t* data, size_t size, bool isSVGA, bool isHud) {
    // 16-byte records: [0]=id, [1:16]=null-terminated ASCII label.
    constexpr size_t kRecordSize = 16;
    if (size == 0 || size % kRecordSize != 0) return;
    size_t count = size / kRecordSize;
    auto& dest = isHud ? (isSVGA ? this->svgaHudDamageLabels : this->vgaHudDamageLabels)
                        : (isSVGA ? this->svgaFrontDamageLabels : this->vgaFrontDamageLabels);
    dest.clear();
    dest.reserve(count);
    for (size_t i = 0; i < count; i++) {
        const uint8_t* r = data + i * kRecordSize;
        WC3DamageLabel label;
        label.id = r[0];
        memcpy(label.label, r + 1, 15);
        label.label[15] = '\0';
        dest.push_back(label);
    }
}

// A plain 13-byte filename string (e.g. "meddeath.iff\0"), not a shape —
// resolves to a second cockpit-family IFF (DATA\COCKPITS\<name>.IFF) whose
// own FORM DETH wraps two "1.11" full-screen animation sequences (DETH =
// death flip-book, EJCT = ejection flip-book). Both are flat entries with
// no leading header (the "1.11" marker sits at offset 0), unlike e.g.
// STAR's glints or an explosion's TXMV.
void RSCockpit::parseWC3_COCK_DETH(uint8_t* data, size_t size) {
    ByteStream stream(data, size);
    this->deathAnimName = stream.ReadStringNoSize(size);
    if (this->deathAnimName.empty()) {
        return;
    }
    std::string tmpname = "..\\..\\DATA\\COCKPITS\\" + this->deathAnimName;
    std::transform(tmpname.begin(), tmpname.end(), tmpname.begin(), ::toupper);
    TreEntry* entry = this->asset_manager->GetEntryByName(tmpname);
    if (entry == nullptr) {
        return;
    }
    // Outer "FORM DETH" wraps the two real sub-chunks (also plain data, not
    // FORMs — SHLD/TARG siblings under the same outer form are empty
    // placeholders on every death-anim asset checked).
    IFFSaxLexer outerLexer;
    std::unordered_map<std::string, std::function<void(uint8_t* data, size_t size)>> outerHandlers;
    outerHandlers["DETH"] = [this](uint8_t* outerData, size_t outerSize) {
        IFFSaxLexer lexer;
        std::unordered_map<std::string, std::function<void(uint8_t* data, size_t size)>> handlers;
        handlers["DETH"] = [this](uint8_t* d, size_t s) {
            RSImageSet* frames = RSWC3DecodeShapeEntry(d, s);
            this->deathFrames = *frames;
            delete frames;
        };
        handlers["EJCT"] = [this](uint8_t* d, size_t s) {
            RSImageSet* frames = RSWC3DecodeShapeEntry(d, s);
            this->ejectFrames = *frames;
            delete frames;
        };
        lexer.InitFromRAM(outerData, outerSize, handlers);
    };
    outerLexer.InitFromRAM(entry->data, entry->size, outerHandlers);
}

void RSCockpit::parseWC3_COCK_SHAP(uint8_t* data, size_t size) {
    std::unordered_map<uint32_t, RSImageSet*> shapes = RSWC3DecodeShapePak(data, size);
    for (auto& [id, img] : shapes) {
        // Keep every id (see WC3CockpitShapeId for what's been visually
        // identified so far) — this used to discard everything except the
        // two full-screen backgrounds.
        this->instrumentShapes[id] = img;
        // The atlas bundles a 320x200 "VGA" background and a 640x480 "SVGA"
        // background (higher resolution, more visible dashboard detail —
        // not simply a 2x upscale, it's a distinct 4:3 canvas); mirror
        // those into ARTP/ARTP_SVGA for the existing render path
        // (SCCockpit::Render/RenderHighResBackground), which expects them
        // there rather than looked up via instrumentShapes. Matched by
        // size, not just id 0/30 — HVYPIT.IFF/BOMPIT.IFF each carry a
        // *second* shape at each background size (ids 2/32 alongside
        // 0/30, likely a damaged/undamaged or day/night variant), and
        // both need to land in ARTP/ARTP_SVGA the way the pre-existing
        // (size-based) version of this function already did.
        if (img->GetNumImages() > 0) {
            RLEShape* shape = img->GetShape(0);
            if (shape->GetWidth() == 320 && shape->GetHeight() == 200) {
                this->ARTP.Add(shape);
            } else if (shape->GetWidth() == 640 && shape->GetHeight() == 480) {
                this->ARTP_SVGA.Add(shape);
            }
        }
    }
}

void RSCockpit::parseINFO(uint8_t* data, size_t size) {
    this->INFO = std::vector<uint8_t>(data, data + size);
}
void RSCockpit::parseARTP(uint8_t* data, size_t size) {
    uint8_t* data2 = (uint8_t*) malloc(size);
    memcpy(data2, data, size);
    PakArchive* pak = new PakArchive();
    
    uint32_t tsize = data2[0] + (data2[1] << 8) + (data2[2] << 16) + (data2[3] << 24);
    pak->InitFromRAM("ARTP", data2, tsize);
    this->ARTP.InitFromPakArchive(pak);
}
void RSCockpit::parseVTMP(uint8_t* data, size_t size) {
    uint8_t* data2 = (uint8_t*) malloc(size);
    memcpy(data2, data+2, size-2);
    this->VTMP.init(data2, size-2);
}
void RSCockpit::parseEJEC(uint8_t* data, size_t size) {
    uint8_t* data2 = (uint8_t*) malloc(size);
    memcpy(data2, data, size);
    PakArchive* pak = new PakArchive();
    pak->InitFromRAM("EJEC", data2, size);
    this->EJEC.InitFromSubPakEntry(pak);
}
void RSCockpit::parseGUNF(uint8_t* data, size_t size) {
    uint8_t* data2 = (uint8_t*) malloc(size);
    memcpy(data2, data, size);
    PakArchive* pak = new PakArchive();
    pak->InitFromRAM("GUNF", data2, size);
    this->GUNF.InitFromSubPakEntry(pak);
}
void RSCockpit::parseGHUD(uint8_t* data, size_t size) {
    uint8_t* data2 = (uint8_t*) malloc(size);
    memcpy(data2, data, size);
    PakArchive* pak = new PakArchive();
    pak->InitFromRAM("GHUD", data2, size);
    this->GHUD.InitFromSubPakEntry(pak);
}
void RSCockpit::parseREAL(uint8_t* data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t* data, size_t size)>> handlers;

    handlers["INFO"] = std::bind(&RSCockpit::parseREAL_INFO, this, std::placeholders::_1, std::placeholders::_2);
    handlers["OBJS"] = std::bind(&RSCockpit::parseREAL_OBJS, this, std::placeholders::_1, std::placeholders::_2);

    lexer.InitFromRAM(data, size, handlers);
}
void RSCockpit::parseREAL_INFO(uint8_t* data, size_t size) {
    REAL.INFO = std::vector<uint8_t>(data, data + size);
}
/**
 * Parse a REAL.OBJS block.
 *
 * This function parses a REAL.OBJS block. This block contains the name of a
 * model file that is to be loaded. The model file is loaded from the
 * OBJECTS.TRE archive and is stored in the REAL.OBJS structure.
 *
 * @param data The data to parse.
 * @param size The size of the data to parse.
 */
void RSCockpit::parseREAL_OBJS(uint8_t* data, size_t size) {
    ByteStream* reader = new ByteStream(data, size);
    std::string name = reader->ReadString(size);
    std::transform(name.begin(), name.end(), name.begin(), ::toupper);
    TreEntry* entry = this->asset_manager->GetEntryByName(this->asset_manager->object_root_path + name + ".IFF");
    REAL.OBJS = new RSEntity();
    REAL.OBJS->InitFromRAM(entry->data, entry->size, this->asset_manager->object_root_path + name + ".IFF");
}
void RSCockpit::parseCHUD(uint8_t* data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t* data, size_t size)>> handlers;
    handlers["FILE"] = std::bind(&RSCockpit::parseCHUD_FILE, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
void RSCockpit::parseCHUD_FILE(uint8_t* data, size_t size) {
    ByteStream* reader = new ByteStream(data, size);
    CHUD.FILE = reader->ReadString(size);
}
void RSCockpit::parseMONI(uint8_t* data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t* data, size_t size)>> handlers;

    handlers["INFO"] = std::bind(&RSCockpit::parseMONI_INFO, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SPOT"] = std::bind(&RSCockpit::parseMONI_SPOT, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SHAP"] = std::bind(&RSCockpit::parseMONI_SHAP, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DAMG"] = std::bind(&RSCockpit::parseMONI_DAMG, this, std::placeholders::_1, std::placeholders::_2);
    handlers["MFDS"] = std::bind(&RSCockpit::parseMONI_MFDS, this, std::placeholders::_1, std::placeholders::_2);
    handlers["INST"] = std::bind(&RSCockpit::parseMONI_INST, this, std::placeholders::_1, std::placeholders::_2);

    lexer.InitFromRAM(data, size, handlers);
}
void RSCockpit::parseMONI_INFO(uint8_t* data, size_t size) {
    this->MONI.INFO = std::vector<uint8_t>(data, data + size);
}
void RSCockpit::parseMONI_SPOT(uint8_t* data, size_t size) {
    this->MONI.SPOT = std::vector<uint8_t>(data, data + size);
}
void RSCockpit::parseMONI_SHAP(uint8_t* data, size_t size) {
    int offset = 20;
    if (data[0] == 'L' && data[1] == 'Z') {
        LZBuffer lz;
        size_t csize=0;
        uint8_t *uncompressed_data = lz.DecodeLZW(data+6, size-6, csize);
        data = uncompressed_data;
        size = csize;
        offset = 8;
    }
	uint8_t *shape_data;
	shape_data = (uint8_t*) malloc(size);
	memcpy(shape_data, data, size);
    // shape 20 byte offset, don't know why
    this->MONI.SHAP.init(shape_data+offset, size - offset);
}
void RSCockpit::parseMONI_DAMG(uint8_t* data, size_t size) {
	uint8_t *data2;
	data2 = (uint8_t*) malloc(size);
	memcpy(data2, data, size);
    this->MONI.DAMG.init(data2, size);
}
/**
 * @brief Parse an IFF MONI_MFDS chunk
 *
 * Parse an IFF MONI_MFDS chunk, which contains the MFDs of the cockpit
 *
 * @param data Pointer to the memory block to parse
 * @param size The size of the block of memory to parse
 */
void RSCockpit::parseMONI_MFDS(uint8_t* data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t* data, size_t size)>> handlers;

    handlers["COMM"] = std::bind(&RSCockpit::parseMONI_MFDS_COMM, this, std::placeholders::_1, std::placeholders::_2);
    handlers["AARD"] = std::bind(&RSCockpit::parseMONI_MFDS_AARD, this, std::placeholders::_1, std::placeholders::_2);
    handlers["AGRD"] = std::bind(&RSCockpit::parseMONI_MFDS_AGRD, this, std::placeholders::_1, std::placeholders::_2);
    handlers["GCAM"] = std::bind(&RSCockpit::parseMONI_MFDS_GCAM, this, std::placeholders::_1, std::placeholders::_2);
    handlers["WEAP"] = std::bind(&RSCockpit::parseMONI_MFDS_WEAP, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DAMG"] = std::bind(&RSCockpit::parseMONI_MFDS_DAMG, this, std::placeholders::_1, std::placeholders::_2);

    lexer.InitFromRAM(data, size, handlers);
}
void RSCockpit::parseMONI_MFDS_COMM(uint8_t* data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t* data, size_t size)>> handlers;
    handlers["INFO"] = std::bind(&RSCockpit::parseMONI_MFDS_COMM_INFO, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
void RSCockpit::parseMONI_MFDS_COMM_INFO(uint8_t* data, size_t size) {
    this->MONI.MFDS.COMM.INFO = std::vector<uint8_t>(data, data + size);
}
void RSCockpit::parseMONI_MFDS_AARD(uint8_t* data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t* data, size_t size)>> handlers;
    handlers["INFO"] = std::bind(&RSCockpit::parseMONI_MFDS_AARD_INFO, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SHAP"] = std::bind(&RSCockpit::parseMONI_MFDS_AARD_SHAP, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
void RSCockpit::parseMONI_MFDS_AARD_INFO(uint8_t* data, size_t size) {
    this->MONI.MFDS.AARD.INFO = std::vector<uint8_t>(data, data + size);
}
void RSCockpit::parseMONI_MFDS_AARD_SHAP(uint8_t* data, size_t size) {
	uint8_t *shape_data;
	shape_data = (uint8_t*) malloc(size);
	memcpy(shape_data, data, size);
    PakArchive* pak = new PakArchive();
    pak->InitFromRAM("AARD",shape_data, size);
    this->MONI.MFDS.AARD.ARTS.InitFromSubPakEntry(pak);
}
void RSCockpit::parseMONI_MFDS_AGRD(uint8_t* data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t* data, size_t size)>> handlers;
    handlers["INFO"] = std::bind(&RSCockpit::parseMONI_MFDS_AGRD_INFO, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SHAP"] = std::bind(&RSCockpit::parseMONI_MFDS_AGRD_SHAP, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
void RSCockpit::parseMONI_MFDS_AGRD_INFO(uint8_t* data, size_t size) {
    this->MONI.MFDS.AGRD.INFO = std::vector<uint8_t>(data, data + size);
}
void RSCockpit::parseMONI_MFDS_AGRD_SHAP(uint8_t* data, size_t size) {
	uint8_t *shape_data;
	shape_data = (uint8_t*) malloc(size);
	memcpy(shape_data, data, size);
    PakArchive* pak = new PakArchive();
    pak->InitFromRAM("AGRD",shape_data, size);
    this->MONI.MFDS.AGRD.ARTS.InitFromSubPakEntry(pak);
}
void RSCockpit::parseMONI_MFDS_GCAM(uint8_t* data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t* data, size_t size)>> handlers;
    handlers["INFO"] = std::bind(&RSCockpit::parseMONI_MFDS_GCAM_INFO, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SHAP"] = std::bind(&RSCockpit::parseMONI_MFDS_GCAM_SHAP, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
void RSCockpit::parseMONI_MFDS_GCAM_INFO(uint8_t* data, size_t size) {
    this->MONI.MFDS.GCAM.INFO = std::vector<uint8_t>(data, data + size);
}
void RSCockpit::parseMONI_MFDS_GCAM_SHAP(uint8_t* data, size_t size) {
	uint8_t *shape_data;
	shape_data = (uint8_t*) malloc(size);
	memcpy(shape_data, data, size);
    PakArchive* pak = new PakArchive();
    pak->InitFromRAM("GCAM",shape_data, size);
    this->MONI.MFDS.GCAM.ARTS.InitFromSubPakEntry(pak);
}
void RSCockpit::parseMONI_MFDS_WEAP(uint8_t* data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t* data, size_t size)>> handlers;
    handlers["INFO"] = std::bind(&RSCockpit::parseMONI_MFDS_WEAP_INFO, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SHAP"] = std::bind(&RSCockpit::parseMONI_MFDS_WEAP_SHAP, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
void RSCockpit::parseMONI_MFDS_WEAP_INFO(uint8_t* data, size_t size) {
    this->MONI.MFDS.WEAP.INFO = std::vector<uint8_t>(data, data + size);
}
void RSCockpit::parseMONI_MFDS_WEAP_SHAP(uint8_t* data, size_t size) {
	uint8_t *shape_data;
	shape_data = (uint8_t*) malloc(size);
	memcpy(shape_data, data, size);
    PakArchive* pak = new PakArchive();
    pak->InitFromRAM("WEAP",shape_data, size);
    this->MONI.MFDS.WEAP.ARTS.InitFromSubPakEntry(pak);
}

void RSCockpit::parseMONI_MFDS_DAMG(uint8_t* data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t* data, size_t size)>> handlers;
    handlers["INFO"] = std::bind(&RSCockpit::parseMONI_MFDS_DAMG_INFO, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SHAP"] = std::bind(&RSCockpit::parseMONI_MFDS_DAMG_SHAP, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
void RSCockpit::parseMONI_MFDS_DAMG_INFO(uint8_t* data, size_t size) {
    this->MONI.MFDS.DAMG.INFO = std::vector<uint8_t>(data, data + size);
}
void RSCockpit::parseMONI_MFDS_DAMG_SHAP(uint8_t* data, size_t size) {
	uint8_t *shape_data;
	shape_data = (uint8_t*) malloc(size);
	memcpy(shape_data, data, size);
    PakArchive* pak = new PakArchive();
    pak->InitFromRAM("DAMG",shape_data, size);
    this->MONI.MFDS.DAMG.ARTS.InitFromSubPakEntry(pak);
}
/**
 * @brief Parse an IFF MONI_INST chunk
 *
 * Parse an IFF MONI_INST chunk, which contains the instruments of the cockpit
 *
 * @param data Pointer to the memory block to parse
 * @param size The size of the block of memory to parse
 */
void RSCockpit::parseMONI_INST(uint8_t* data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t* data, size_t size)>> handlers;

    handlers["RAWS"] = std::bind(&RSCockpit::parseMONI_INST_RAWS, this, std::placeholders::_1, std::placeholders::_2);
    handlers["ALTI"] = std::bind(&RSCockpit::parseMONI_INST_ALTI, this, std::placeholders::_1, std::placeholders::_2);
    handlers["AIRS"] = std::bind(&RSCockpit::parseMONI_INST_AIRS, this, std::placeholders::_1, std::placeholders::_2);
    handlers["MWRN"] = std::bind(&RSCockpit::parseMONI_INST_MWRN, this, std::placeholders::_1, std::placeholders::_2);

    lexer.InitFromRAM(data, size, handlers);
}
void RSCockpit::parseMONI_INST_RAWS(uint8_t* data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t* data, size_t size)>> handlers;
    handlers["INFO"] = std::bind(&RSCockpit::parseMONI_INST_RAWS_INFO, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SHAP"] = std::bind(&RSCockpit::parseMONI_INST_RAWS_SHAP, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
void RSCockpit::parseMONI_INST_RAWS_INFO(uint8_t* data, size_t size) {
    this->MONI.INST.RAWS.INFO = std::vector<uint8_t>(data, data + size);
    ByteStream* reader = new ByteStream(data, size);
    this->MONI.INST.RAWS.width = reader->ReadUShort();
    this->MONI.INST.RAWS.height = reader->ReadUShort();
    this->MONI.INST.RAWS.zoom_x = reader->ReadUShort(); // skip
    this->MONI.INST.RAWS.zoom_y = reader->ReadUShort(); // skip
    reader->ReadUShort(); // skip
    reader->ReadUShort(); // skip
    reader->ReadUShort(); // skip
    reader->ReadUShort(); // skip
    reader->ReadShort();
    reader->ReadShort();
    
    this->MONI.INST.RAWS.x = reader->ReadShort();
    this->MONI.INST.RAWS.y = reader->ReadShort();
    
    
}
void RSCockpit::parseMONI_INST_RAWS_SHAP(uint8_t* data, size_t size) {

    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t* data, size_t size)>> handlers;
    handlers["SYMB"] = std::bind(&RSCockpit::parseMONI_INST_RAWS_SHAP_SYMB, this, std::placeholders::_1, std::placeholders::_2);
    handlers["ZOOM"] = std::bind(&RSCockpit::parseMONI_INST_RAWS_SHAP_ZOOM, this, std::placeholders::_1, std::placeholders::_2);
    handlers["NORM"] = std::bind(&RSCockpit::parseMONI_INST_RAWS_SHAP_NORM, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);

	
}
void RSCockpit::parseMONI_INST_RAWS_SHAP_SYMB(uint8_t* data, size_t size) {
	uint8_t *shape_data;
	shape_data = (uint8_t*) malloc(size);
	memcpy(shape_data, data, size);
    PakArchive* pak = new PakArchive();
    pak->InitFromRAM("SYMB",shape_data, size);
    this->MONI.INST.RAWS.SYMB.InitFromSubPakEntry(pak);
}
void RSCockpit::parseMONI_INST_RAWS_SHAP_ZOOM(uint8_t* data, size_t size) {
    int offset = 0;
    if (data[0] == 'L' && data[1] == 'Z') {
        LZBuffer lz;
        size_t csize=0;
        uint8_t *uncompressed_data = lz.DecodeLZW(data+6, size-6, csize);
        data = uncompressed_data;
        size = csize;
        offset = 8;
    }
	uint8_t *shape_data;
	shape_data = (uint8_t*) malloc(size);
	memcpy(shape_data, data, size);
    this->MONI.INST.RAWS.ZOOM.init(shape_data+offset, size - offset);
}
void RSCockpit::parseMONI_INST_RAWS_SHAP_NORM(uint8_t* data, size_t size) {
    int offset = 0;
    uint8_t *shape_data;
    if (data[0] == 'L' && data[1] == 'Z') {
        LZBuffer lz;
        size_t csize=0;
        uint8_t *uncompressed_data = lz.DecodeLZW(data+6, size-6, csize);
        data = uncompressed_data;
        size = csize;
        offset = 8;
        shape_data = (uint8_t*) malloc(size);
        memcpy(shape_data, data, size);
        // shape 20 byte offset, don't know why
        this->MONI.INST.RAWS.NORM.init(shape_data+offset, size - offset);
    } else {
        shape_data = (uint8_t*) malloc(size);
        memcpy(shape_data, data, size);
        PakArchive* pak = new PakArchive();
        pak->InitFromRAM("ARTP", shape_data, size);
        RSImageSet *imgset = new RSImageSet();
        imgset->InitFromSubPakEntry(pak);
        this->MONI.INST.RAWS.NORM = *imgset->GetShape(0);
    }
}
void RSCockpit::parseMONI_INST_ALTI(uint8_t* data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t* data, size_t size)>> handlers;
    handlers["INFO"] = std::bind(&RSCockpit::parseMONI_INST_ALTI_INFO, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SHAP"] = std::bind(&RSCockpit::parseMONI_INST_ALTI_SHAP, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
void RSCockpit::parseMONI_INST_ALTI_INFO(uint8_t* data, size_t size) {
    this->MONI.INST.ALTI.INFO = std::vector<uint8_t>(data, data + size);
    ByteStream* reader = new ByteStream(data, size);
    this->MONI.INST.ALTI.x = reader->ReadUShort();
    this->MONI.INST.ALTI.y = reader->ReadUShort();
}
void RSCockpit::parseMONI_INST_ALTI_SHAP(uint8_t* data, size_t size) {
	uint8_t *data2;
	data2 = (uint8_t*) malloc(size);
	memcpy(data2, data, size);
    PakArchive* pak = new PakArchive();
    pak->InitFromRAM("ARTP", data2, size);
    this->MONI.INST.ALTI.ARTS.InitFromSubPakEntry(pak);
}
void RSCockpit::parseMONI_INST_AIRS(uint8_t* data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t* data, size_t size)>> handlers;
    handlers["INFO"] = std::bind(&RSCockpit::parseMONI_INST_AIRS_INFO, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SHAP"] = std::bind(&RSCockpit::parseMONI_INST_AIRS_SHAP, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}

void RSCockpit::parseMONI_INST_AIRS_INFO(uint8_t* data, size_t size) {
    this->MONI.INST.AIRS.INFO = std::vector<uint8_t>(data, data + size);
    ByteStream* reader = new ByteStream(data, size);
    this->MONI.INST.AIRS.x = reader->ReadUShort();
    this->MONI.INST.AIRS.y = reader->ReadUShort();
}
void RSCockpit::parseMONI_INST_AIRS_SHAP(uint8_t* data, size_t size) {
	uint8_t *data2;
	data2 = (uint8_t*) malloc(size);
	memcpy(data2, data, size);
    PakArchive* pak = new PakArchive();
    pak->InitFromRAM("ARTP", data2, size);
    this->MONI.INST.AIRS.ARTS.InitFromSubPakEntry(pak);
}
void RSCockpit::parseMONI_INST_MWRN(uint8_t* data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t* data, size_t size)>> handlers;
    handlers["INFO"] = std::bind(&RSCockpit::parseMONI_INST_MWRN_INFO, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SHAP"] = std::bind(&RSCockpit::parseMONI_INST_MWRN_SHAP, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
void RSCockpit::parseMONI_INST_MWRN_INFO(uint8_t* data, size_t size) {
    this->MONI.INST.MWRN.INFO = std::vector<uint8_t>(data, data + size);
}
void RSCockpit::parseMONI_INST_MWRN_SHAP(uint8_t* data, size_t size) {
    uint8_t *data2;
	data2 = (uint8_t*) malloc(size);
	memcpy(data2, data, size);
    PakArchive* pak = new PakArchive();
    pak->InitFromRAM("ARTP", data2, size);
    this->MONI.INST.MWRN.ARTS.InitFromSubPakEntry(pak);
}
void RSCockpit::parseFADE(uint8_t* data, size_t size) {
    this->FADE = std::vector<uint8_t>(data, data + size);
}
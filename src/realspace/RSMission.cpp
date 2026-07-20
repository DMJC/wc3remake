#include "RSMission.h"
#include "block_def.h"
#include "RSWC3Shape.h"
#include <cstring>

// AREA position is stored on a coarser scale than PART/SPOT position (which
// use BLOCK_COORD_SCALE/HEIGH_MAP_SCALE, both currently 1.0 — i.e. raw file
// units). Empirically: within a single AREA, that area's own PART members
// already spread across millions of raw units (e.g. MISNA003 area 4's two
// "darket" members are ~2.5M apart in x), while AREA-to-AREA position deltas
// read with a 1.0 scale are only hundreds to low-thousands apart — meaning
// distinct named zones (e.g. MISNA003's "Escort 1"/"Escort 2"/"Jump Point")
// would end up almost coincident in world space instead of being the
// separate travel destinations their names and the OP_SET_OBJ_FLY_TO_AREA/
// "Fly to Way Area" mission-objective mechanic imply.
// 256.0 (2^8) is confirmed against live gameplay: in MISNA003, radar-read
// distances from the mission-start position to Escort 1/Escort 2/Jump Point
// (~206000/~570000/~800000 units) divided by this chunk's raw (unscaled)
// AREA-to-AREA distances (~799/~2180/~2996) give ratios of 257.7/261.4/267.0
// — clustered tightly around 256, well within the slop of "approximately"
// eyeballed radar readings.
static const float MISN_AREA_POSITION_SCALE = 256.0f;

// PART x/z are a 24-bit fixed-point value with a 16-bit fraction (raw value
// is a near-exact multiple of 65536 in the large majority of real records —
// e.g. a recurring "tdest" destroyer placement's z=6,815,744 is exactly
// 104.0*65536), unlike AREA's plain-integer position field above. Confirmed
// against live gameplay radar readings: that same tdest offset (x=0,
// z=6,815,744) — a CAST slot player-identified as the destroyer "Coventry" —
// read as ~16200-16500 units on the in-game radar across two missions,
// giving 155.8-158.7 real-units per fixed-point-unit (i.e. per raw/65536).
// A second, rougher reading (a "directly ahead" darket, raw z=-5,767,168 ==
// -88.0 fixed-point, radar-estimated at "~15000 but likely lower") is
// consistent with the same range once adjusted down. 158.0 is a working
// point estimate within that confirmed band, not a fully pinned constant —
// refine with more readings if precision matters.
static const float MISN_PART_POSITION_SCALE = 158.0f / 65536.0f;

void missionstrtoupper(char *src) {
    int i = 0;
    for (int i = 0; i < strlen(src); i++) {
        src[i] = toupper(src[i]);
    }
}

RSMission::RSMission() { printf("CREATE MISSION\n"); }

RSMission::~RSMission() {}

MISN_PART *RSMission::getPlayerCoord() { 
    int search_id = 0;
    if (this->player != nullptr) {
        return this->player;
    }
    for (auto cast : this->mission_data.casting) {
        if (cast->actor == "PLAYER") {
            for (auto part : this->mission_data.parts) {
                if (part->id == search_id) {
                    this->player = part;
                    return part;
                }
            }
        }
        search_id++;
    }
    return nullptr;
}
MISN_PART *RSMission::getObject(const char *name) { 
    for (auto obj : this->mission_data.parts) {
        if (obj->member_name == name) {
            return obj;
        }
    }
    return nullptr;
}

void RSMission::InitFromRAM(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;
    this->player = nullptr;
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["MISN"] = std::bind(&RSMission::parseMISN, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}

void RSMission::parseMISN(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;

    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["VERS"] = std::bind(&RSMission::parseMISN_VERS, this, std::placeholders::_1, std::placeholders::_2);
    handlers["INFO"] = std::bind(&RSMission::parseMISN_INFO, this, std::placeholders::_1, std::placeholders::_2);
    handlers["TUNE"] = std::bind(&RSMission::parseMISN_TUNE, this, std::placeholders::_1, std::placeholders::_2);
    handlers["NAME"] = std::bind(&RSMission::parseMISN_NAME, this, std::placeholders::_1, std::placeholders::_2);
    handlers["COCK"] = std::bind(&RSMission::parseMISN_COCK, this, std::placeholders::_1, std::placeholders::_2);
    handlers["WRLD"] = std::bind(&RSMission::parseMISN_WRLD, this, std::placeholders::_1, std::placeholders::_2);

    handlers["AREA"] = std::bind(&RSMission::parseMISN_AREA, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SPOT"] = std::bind(&RSMission::parseMISN_SPOT, this, std::placeholders::_1, std::placeholders::_2);
    handlers["NUMS"] = std::bind(&RSMission::parseMISN_NUMS, this, std::placeholders::_1, std::placeholders::_2);
    handlers["MSGS"] = std::bind(&RSMission::parseMISN_MSGS, this, std::placeholders::_1, std::placeholders::_2);
    // French/German translations of MSGS — same null-separated-string-list
    // shape and ordering, confirmed against every mission file in
    // MISSIONS.TRE/cd4miss.tre (message count matches MSGS 1:1 in 77/83
    // missions; the handful of mismatches are editorial, not structural).
    // Previously unregistered entirely, so this text was silently dropped
    // instead of being available for French/German localization.
    handlers["MSGF"] = std::bind(&RSMission::parseMISN_MSGF, this, std::placeholders::_1, std::placeholders::_2);
    handlers["MSGG"] = std::bind(&RSMission::parseMISN_MSGG, this, std::placeholders::_1, std::placeholders::_2);
    handlers["FLAG"] = std::bind(&RSMission::parseMISN_FLAG, this, std::placeholders::_1, std::placeholders::_2);
    handlers["CAST"] = std::bind(&RSMission::parseMISN_CAST, this, std::placeholders::_1, std::placeholders::_2);
    handlers["PROG"] = std::bind(&RSMission::parseMISN_PROG, this, std::placeholders::_1, std::placeholders::_2);
    handlers["PART"] = std::bind(&RSMission::parseMISN_PART, this, std::placeholders::_1, std::placeholders::_2);
    handlers["TEAM"] = std::bind(&RSMission::parseMISN_TEAM, this, std::placeholders::_1, std::placeholders::_2);
    handlers["PLAY"] = std::bind(&RSMission::parseMISN_PLAY, this, std::placeholders::_1, std::placeholders::_2);
    handlers["LOAD"] = std::bind(&RSMission::parseMISN_LOAD, this, std::placeholders::_1, std::placeholders::_2);
    handlers["CACH"] = std::bind(&RSMission::parseMISN_CACH, this, std::placeholders::_1, std::placeholders::_2);
    // MISN>NAV — real per-mission nav-map data, previously entirely
    // unparsed (no handler was ever registered for it at all). See
    // MISN_NAV_RES's own comment in RSMission.h. The raw 4-byte tag is
    // "NAV" + a NULL padding byte, not a space — confirmed by decoding the
    // raw chunk bytes directly (see the identical mistake caught and fixed
    // in RSCockpit.cpp's own "VGA"/parseWC3_COCK comment for why a plain
    // `handlers["NAV "]` would silently never match).
    handlers[std::string("NAV", 4)] = std::bind(&RSMission::parseMISN_NAV, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}

void RSMission::parseMISN_VERS(uint8_t *data, size_t size) {
    ByteStream stream(data, size);
    this->mission_data.version = stream.ReadUShort();
}
void RSMission::parseMISN_INFO(uint8_t *data, size_t size) {
    ByteStream stream(data, size);
    if (size > 0) {
        this->mission_data.info = stream.ReadString(size);
    }
}
void RSMission::parseMISN_TUNE(uint8_t *data, size_t size) {
    ByteStream stream(data, size);
    this->mission_data.tune = stream.ReadByte();
}
void RSMission::parseMISN_NAME(uint8_t *data, size_t size) {
    ByteStream stream(data, size);
    for (int i = 0; i < size; i++) {
        this->mission_data.name.push_back(stream.ReadByte());
    }
    std::transform(this->mission_data.name.begin(), this->mission_data.name.end(), this->mission_data.name.begin(), ::toupper);
}
void RSMission::parseMISN_COCK(uint8_t *data, size_t size) {
    ByteStream stream(data, size);
    this->mission_data.cockpit_name = stream.ReadStringNoSize(size);
    std::transform(this->mission_data.cockpit_name.begin(), this->mission_data.cockpit_name.end(),
                    this->mission_data.cockpit_name.begin(), ::toupper);
}
void RSMission::parseMISN_WRLD(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;

    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["FILE"] = std::bind(&RSMission::parseMISN_WRLD_FILE, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
void RSMission::parseMISN_WRLD_FILE(uint8_t *data, size_t size) {
    ByteStream stream(data, size);
    this->mission_data.world_filename = stream.ReadStringNoSize(size);
}
void RSMission::parseMISN_AREA(uint8_t *data, size_t size) {
    ByteStream stream(data, size);

    size_t read = 0;
    uint8_t buffer;
    int cpt = 0;
    while (stream.IsEndOfStream() == false) {
        buffer = stream.ReadByte();
        AREA *tmparea = new AREA();

        tmparea->id = ++cpt;
        tmparea->AreaType = '\0';
        switch (buffer) {
        case 'S':
            tmparea->AreaType = 'S';
            for (int k = 0; k < 33; k++) {
                tmparea->AreaName[k] = stream.ReadByte();
            }
            missionstrtoupper(tmparea->AreaName);
            tmparea->position.x = stream.ReadInt24LE() * MISN_AREA_POSITION_SCALE;
            tmparea->position.z = -stream.ReadInt24LE() * MISN_AREA_POSITION_SCALE;
            tmparea->position.y = stream.ReadInt24LE() * MISN_AREA_POSITION_SCALE;

            tmparea->AreaWidth = stream.ReadUShort();
            tmparea->AreaHeight = tmparea->AreaWidth;
            tmparea->unknown_bytes.push_back(stream.ReadByte());
            break;
        case 'C':
            tmparea->AreaType = 'C';
            for (int k = 0; k < 33; k++) {
                tmparea->AreaName[k] = stream.ReadByte();
            }
            missionstrtoupper(tmparea->AreaName);
            tmparea->position.x = stream.ReadInt24LE() * MISN_AREA_POSITION_SCALE;
            tmparea->position.z = -stream.ReadInt24LE() * MISN_AREA_POSITION_SCALE;
            tmparea->position.y = stream.ReadInt24LE() * MISN_AREA_POSITION_SCALE;

            
            tmparea->AreaWidth = stream.ReadUShort() * (uint16_t)(BLOCK_COORD_SCALE);

            // unsigned int Blank0; // off 48-49
            tmparea->unknown_bytes.push_back(stream.ReadByte());
            tmparea->unknown_bytes.push_back(stream.ReadByte());
            tmparea->AreaHeight = stream.ReadUShort() * (int) HEIGH_MAP_SCALE;
            // unsigned char Blank1; // off 52
            tmparea->unknown_bytes.push_back(stream.ReadByte());
            break;
        case 'B':
            tmparea->AreaType = 'B';
            for (int k = 0; k < 33; k++) {
                tmparea->AreaName[k] = stream.ReadByte();
            }
            missionstrtoupper(tmparea->AreaName);
            tmparea->position.x = stream.ReadInt24LE() * MISN_AREA_POSITION_SCALE;
            tmparea->position.z = -stream.ReadInt24LE() * MISN_AREA_POSITION_SCALE;
            tmparea->position.y = stream.ReadInt24LE() * MISN_AREA_POSITION_SCALE;

            tmparea->AreaWidth = stream.ReadUShort() * (uint16_t)(BLOCK_COORD_SCALE);

            // unsigned char Blank0[10]; // off 48-59
            for (int k = 0; k < 10; k++) {
                tmparea->unknown_bytes.push_back(stream.ReadByte());
            }
            // unsigned int AreaHeight; // off 60-61
            tmparea->AreaHeight = stream.ReadUShort()  * (int) HEIGH_MAP_SCALE;

            // unsigned char Unknown[5]; // off 62-67
            for (int k = 0; k < 5; k++) {
                tmparea->unknown_bytes.push_back(stream.ReadByte());
            }
            break;
        default:
            stream.ReadByte();
            printf("ERROR IN PARSING AREA\n");
            break;
        }
        if (tmparea->AreaType != '\0') {
            this->mission_data.areas.push_back(tmparea);
        }
    }
}
int32_t ReadInt24LE_fromVec(std::vector<uint8_t>data, int offset) {
    int32_t i = 0;
    uint8_t buffer[4];
    buffer[0] = data[offset];
    buffer[1] = data[offset+1];
    buffer[2] = data[offset+2];
    buffer[3] = data[offset+3];
    i = (buffer[2] << 16) | (buffer[1] << 8) | (buffer[0] << 0);
    if (buffer[2] & 0x80) {
        i = (0xff << 24) | i;
    }
    return i;
}
void RSMission::parseMISN_SPOT(uint8_t *data, size_t size) {
    size_t numParts = size / 14;
    ByteStream stream(data, size);
    for (int i = 0; i < numParts; i++) {
        SPOT *spt = new SPOT();
        if (spt != NULL) {
            spt->id = i;
            spt->area_id = 0;
            spt->area_id |= stream.ReadByte() << 0;
            spt->area_id |= stream.ReadByte() << 8;

            spt->unknown1 = stream.ReadByte();
            int32_t x, z;
            int16_t y;
            x = stream.ReadInt24LE();
            z = stream.ReadInt24LE();
            y = 0;
            y |= stream.ReadByte() << 0;
            y |= stream.ReadByte() << 8;
            spt->position = Vector3D(x * BLOCK_COORD_SCALE, y * HEIGH_MAP_SCALE, -z * BLOCK_COORD_SCALE);
            
            spt->unknown2 = stream.ReadByte();
            this->mission_data.spots.push_back(spt);
        }
    }
}
void RSMission::parseMISN_NUMS(uint8_t *data, size_t size) {
    ByteStream stream(data, size);
    for (int i = 0; i < size; i++) {
        this->mission_data.nums.push_back(stream.ReadByte());
    }
}
void RSMission::parseMISN_MSGS_into(uint8_t *data, size_t size, std::vector<std::string *> &target) {
    size_t read = 0;
    while (read < size) {

        char buffer[512];
        int i = 0;
        while (data[read] != '\0') {
            buffer[i]= data[read];
            read++;
            i++;
        }
        if (data[read] == '\0') {
            buffer[i] = '\0';
            std::string *msg = new std::string(buffer);
            read++;
            target.push_back(msg);
        }
    }
}
void RSMission::parseMISN_MSGS(uint8_t *data, size_t size) {
    parseMISN_MSGS_into(data, size, this->mission_data.messages);
}
void RSMission::parseMISN_MSGF(uint8_t *data, size_t size) {
    parseMISN_MSGS_into(data, size, this->mission_data.messages_fr);
}
void RSMission::parseMISN_MSGG(uint8_t *data, size_t size) {
    parseMISN_MSGS_into(data, size, this->mission_data.messages_de);
}
void RSMission::parseMISN_FLAG(uint8_t *data, size_t size) {
    if (size == 0) {
        this->mission_data.flags.clear();
        this->mission_data.flags.reserve(65536);
        for (int i = 0; i < 65536; i++) {
            this->mission_data.flags.push_back(0);
        }
        return;
    }
    ByteStream stream(data, size);
    stream.ReadByte(); // skip first byte
    size_t read = 1;
    this->mission_data.flags.push_back(0);
    while (read < size-2) {
        int16_t flag = 0;
        stream.ReadByte(); // skip first byte
        flag = stream.ReadByte();
        this->mission_data.flags.push_back(flag);
        read += 2;
    }
}
void RSMission::parseMISN_CAST(uint8_t *data, size_t size) {
    // Record width is 13 bytes (8-byte name + 1 pad byte + a 4-byte field
    // this parser used to skip entirely), not 9 — confirmed empirically:
    // with a 9-byte stride, every name past the first read garbage; with 13
    // bytes, all of them decode cleanly (e.g. "PLAYER", "HOBBES", and,
    // critically, an actor named "VICA1"/"VICB1"/"VICD1"/"VICL4" — the
    // "VIC<mission-series><n>" prefix that WC3 uses in place of Strike
    // Commander's fixed "STRIBASE" to name the TCS Victory itself, one per
    // mission — see WC3Mission::loadMission()'s cast->actor handling).
    size_t nbactor = size / 13;
    ByteStream stream(data, size);

    for (int i = 0; i < nbactor; i++) {
        CAST *tmpcast = new CAST();
        std::string actor = stream.ReadString(8);
        std::transform(actor.begin(), actor.end(), actor.begin(), ::toupper);

        tmpcast->actor = actor;
        tmpcast->profile = nullptr;
        stream.ReadByte();
        tmpcast->unknown0 = stream.ReadUInt32LE();
        this->mission_data.casting.push_back(tmpcast);
    }
}
void RSMission::paseMissionScript(uint8_t *data, size_t size) {
    // 8 bytes/instruction: opcode(1) + pad(1) + marker(2, always 0xED,0xFE)
    // + value(4, signed LE). See the PROG struct comment in RSMission.h.
    ByteStream stream(data, size);
    std::vector<PROG> *prog;
    prog = new std::vector<PROG>();
    size_t count = size / 8;
    for (size_t i = 0; i < count; i++) {
        PROG tmp;
        tmp.opcode = stream.ReadByte();
        stream.ReadByte(); // pad, always 0
        stream.ReadByte(); // marker byte 0 (0xED)
        stream.ReadByte(); // marker byte 1 (0xFE)
        tmp.arg = stream.ReadInt32LE();
        if (tmp.opcode == 0x00 && tmp.arg == 0) {
            this->mission_data.prog.push_back(prog);
            prog = new std::vector<PROG>();
        } else {
            prog->push_back(tmp);
        }
    }
    // Push the final block regardless of whether it had a trailing separator.
    // If it's non-empty, this recovers instructions that would otherwise be
    // silently dropped ("MISN code skipping"). If it's empty (the normal case
    // when data ends with a separator), this is just a harmless empty entry.
    this->mission_data.prog.push_back(prog);
}
void RSMission::parseMISN_PROG(uint8_t *data, size_t size) {
    this->paseMissionScript(data, size);
}
void RSMission::parseMISN_PART(uint8_t *data, size_t size) {
    ByteStream stream(data, size);
    size_t numParts = size / 62;
    for (int i = 0; i < numParts; i++) {
        MISN_PART *prt = new MISN_PART();
        prt->id = 0;
        prt->id |= stream.ReadByte() << 0;
        prt->id |= stream.ReadByte() << 8;
        prt->unknown0 = 0;
        prt->unknown0 |= stream.ReadByte() << 0;
        prt->unknown0 |= stream.ReadByte() << 8;
        prt->member_name = stream.ReadString(8);
        std::transform(prt->member_name.begin(), prt->member_name.end(), prt->member_name.begin(), ::toupper);
        prt->member_name_destroyed = stream.ReadString(8);
        std::transform(prt->member_name_destroyed.begin(), prt->member_name_destroyed.end(), prt->member_name_destroyed.begin(), ::toupper);
        prt->weapon_load = stream.ReadString(8);
        std::transform(prt->weapon_load.begin(), prt->weapon_load.end(), prt->weapon_load.begin(), ::toupper);
        
        prt->area_id = stream.ReadByte();
        prt->unknown1 = stream.ReadByte();
        prt->unknown2 = 0;
        prt->unknown2 |= stream.ReadByte() << 0;
        prt->unknown2 |= stream.ReadByte() << 8;
        int32_t x, z;
        int16_t y;
        // ReadInt24LE() actually consumes 4 bytes (reads an unused buffer[3]
        // it never folds into the result) — fine for formats with a real
        // alignment/padding byte after each 24-bit field, but PART records
        // have no room for one: the field layout only reconciles to the
        // true 62-byte record width (id 2 + unknown0 2 + member_name 8 +
        // member_name_destroyed 8 + weapon_load 8 + area_id 1 + unknown1 1 +
        // unknown2 2 + x 3 + z 3 + y 2 + unknown3 1 + azymuth 2 +
        // unknown_bytes 11 + progs_id 8 = 62) if x/z are real 3-byte reads.
        // Use the sibling that actually reads 3.
        x = stream.ReadInt24LEByte3();
        z = stream.ReadInt24LEByte3();
        y = 0;
        y |= stream.ReadByte() << 0;
        y |= stream.ReadByte() << 8;
        // Y left on the old (1.0) scale, unlike x/z: y is only a 16-bit field
        // (not the 24-bit fixed-point encoding x/z show), and real values
        // seen (-59, 97, -99, ...) don't fit the same /65536 pattern — likely
        // a different, smaller-magnitude unit, not yet investigated.
        prt->position = Vector3D(x * MISN_PART_POSITION_SCALE, y * HEIGH_MAP_SCALE, -z * MISN_PART_POSITION_SCALE);

        prt->unknown3 = stream.ReadByte();
        prt->azymuth = 0;
        prt->azymuth |= stream.ReadByte() << 0;
        prt->azymuth |= stream.ReadByte() << 8;
        for (int k = 0; k < 11; k++) {
            prt->unknown_bytes.push_back(stream.ReadByte());
        }
        for (int k = 0; k < 4; k++) {
            prt->progs_id.push_back(stream.ReadByte());
            stream.ReadByte();
        }
        prt->on_is_activated = prt->progs_id[0];
        prt->on_mission_update = prt->progs_id[1];
        prt->on_is_destroyed = prt->progs_id[2];
        prt->on_missions_init = prt->progs_id[3];
        prt->entity = nullptr;
        this->mission_data.parts.push_back(prt);
    }
}
void RSMission::parseMISN_TEAM(uint8_t *data, size_t size) {
    ByteStream stream(data, size);
    size_t read = 0;
    while (read < size) {
        uint16_t buffer = stream.ReadShort();
        read+=2;
        this->mission_data.team.push_back(buffer);
    }
}
void RSMission::parseMISN_PLAY(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;

    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["SCNE"] = std::bind(&RSMission::parseMISN_PLAY_SCEN, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
void RSMission::parseMISN_PLAY_SCEN(uint8_t *data, size_t size) {
    MISN_SCEN *scen = new MISN_SCEN();
    ByteStream stream(data, size);
    scen->is_active = stream.ReadByte();
    scen->area_id = stream.ReadShort();
    int16_t prog_id = 0;
    for (int i = 0 ; i<3; i++) {
        prog_id = stream.ReadShort();
        scen->progs_id.push_back(prog_id);
    }
    scen->on_is_activated = scen->progs_id[0];
    scen->on_leaving = scen->progs_id[1];
    scen->on_mission_update = scen->progs_id[2];
    scen->is_coord_on_area = stream.ReadByte();
    for (int i = 0; i < 14; i++) {
        scen->unknown_bytes.push_back(stream.ReadByte());
    }
    size_t read = 24;
    // The remaining bytes are NOT a byte+padding-byte cast list — confirmed
    // empirically by decoding misna001's 5 scenes: the real layout is a
    // 10-byte preamble (a 2-byte tag/flag whose meaning isn't determined,
    // then two int32 world coordinates in the same fixed-point encoding as
    // MISN_PART's own x/z) followed by the cast list as 4-byte
    // little-endian integers, one per CAST array index this scene
    // spawns/activates. The old byte+pad reading produced nonsense
    // (garbage interleaved with a few coincidentally-plausible low bytes)
    // instead of the clean small indices (e.g. scene 1 -> 0,1,2,3,5 ==
    // PLAYER/TDESTROY/TDESTRO2/TCRUISER/VICA1) this decoding yields.
    if (read + 2 <= size) {
        stream.ReadShort(); // tag/flags, meaning not yet determined
        read += 2;
    }
    int32_t spawn_x = 0, spawn_z = 0;
    if (read + 4 <= size) {
        spawn_x = stream.ReadInt32LE();
        read += 4;
    }
    if (read + 4 <= size) {
        spawn_z = stream.ReadInt32LE();
        read += 4;
    }
    scen->spawn_position = Vector3D(spawn_x * MISN_PART_POSITION_SCALE, 0, -spawn_z * MISN_PART_POSITION_SCALE);
    while (read + 4 <= size) {
        scen->cast.push_back(stream.ReadInt32LE());
        read += 4;
    }
    this->mission_data.scenes.push_back(scen);
}
void RSMission::parseMISN_LOAD(uint8_t *data, size_t size) {

}
void RSMission::parseMISN_CACH(uint8_t *data, size_t size) {

}
void RSMission::parseMISN_NAV(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    // "VGA" is null-padded, not space-padded — see this function's own
    // registration in parseMISN for why that matters.
    handlers[std::string("VGA", 4)] = [this](uint8_t* d, size_t s) { this->parseMISN_NAV_RES(d, s, false); };
    handlers["SVGA"] = [this](uint8_t* d, size_t s) { this->parseMISN_NAV_RES(d, s, true); };
    lexer.InitFromRAM(data, size, handlers);
}
void RSMission::parseMISN_NAV_RES(uint8_t *data, size_t size, bool isSVGA) {
    MISN_NAV_RES &out = isSVGA ? this->mission_data.navSVGA : this->mission_data.navVGA;
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["DATA"] = [this, &out](uint8_t* d, size_t s) { this->parseMISN_NAV_RES_DATA(d, s, out); };
    handlers["TEXT"] = [this, &out](uint8_t* d, size_t s) { this->parseMISN_NAV_RES_TEXT(d, s, out.textLines_en); };
    handlers["TEXF"] = [this, &out](uint8_t* d, size_t s) { this->parseMISN_NAV_RES_TEXT(d, s, out.textLines_fr); };
    handlers["TEXG"] = [this, &out](uint8_t* d, size_t s) { this->parseMISN_NAV_RES_TEXT(d, s, out.textLines_de); };
    handlers["OBJT"] = [this, &out](uint8_t* d, size_t s) { this->parseMISN_NAV_RES_OBJ(d, s, out.objectiveLabels_en); };
    handlers["OBJF"] = [this, &out](uint8_t* d, size_t s) { this->parseMISN_NAV_RES_OBJ(d, s, out.objectiveLabels_fr); };
    handlers["OBJG"] = [this, &out](uint8_t* d, size_t s) { this->parseMISN_NAV_RES_OBJ(d, s, out.objectiveLabels_de); };
    handlers["PLYR"] = [&out](uint8_t* d, size_t s) { out.playerIcon = RSWC3DecodeShapeEntry(d, s); };
    // "VDU" is null-padded, not space-padded — see parseMISN_NAV's own
    // comment for the general gotcha this class of bug falls into.
    handlers[std::string("VDU", 4)] = [&out](uint8_t* d, size_t s) { out.background = RSWC3DecodeShapeEntry(d, s); };
    lexer.InitFromRAM(data, size, handlers);
}
void RSMission::parseMISN_NAV_RES_DATA(uint8_t *data, size_t size, MISN_NAV_RES &out) {
    if (size < 16) {
        return;
    }
    ByteStream bs(data, size);
    bs.ReadInt32LE(); // x0, always 0 on every file checked
    bs.ReadInt32LE(); // y0, always 0
    out.width = bs.ReadInt32LE();
    out.height = bs.ReadInt32LE();
    // Remaining fields (a constant 100, a VGA=1/SVGA=2 resolution-mode
    // echo, one more mode-ish byte, and the "PLYR" tag re-encoded as an
    // int32) aren't needed for anything this reads yet — width/height are
    // the only fields actually consumed (canvas size for the background
    // image already carried by VDU, so this is really just a redundant
    // confirmation of that image's own dimensions).
}
// TEXT/TEXF/TEXG: 4 fixed 40-byte records — see MISN_NAV_RES's own comment
// for the (unconfirmed) leading 8-byte header and the [title, mission-
// format, objective-format, notes-format] line order.
void RSMission::parseMISN_NAV_RES_TEXT(uint8_t *data, size_t size, std::vector<std::string> &lines) {
    const size_t kRecordSize = 40;
    const size_t kStringOffset = 8;
    const size_t kStringLen = 32;
    lines.clear();
    for (size_t off = 0; off + kRecordSize <= size; off += kRecordSize) {
        const char* str = (const char*)(data + off + kStringOffset);
        size_t len = strnlen(str, kStringLen);
        lines.emplace_back(str, len);
    }
}
// OBJT/OBJF/OBJG: 10 fixed 32-byte null-padded strings, no per-record
// header (unlike TEXT/TEXF/TEXG above) — confirmed by direct byte
// inspection (MISNA001.IFF's OBJT decodes cleanly as "Unknown"/"Take
// Off"/"Landing"/... with zero padding directly after each string, no
// stray header bytes).
void RSMission::parseMISN_NAV_RES_OBJ(uint8_t *data, size_t size, std::vector<std::string> &labels) {
    const size_t kRecordSize = 32;
    labels.clear();
    for (size_t off = 0; off + kRecordSize <= size; off += kRecordSize) {
        const char* str = (const char*)(data + off);
        size_t len = strnlen(str, kRecordSize);
        labels.emplace_back(str, len);
    }
}
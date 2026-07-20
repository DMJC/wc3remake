#include "WC3LoadoutData.h"
#include <cstring>

// LOADOUT.IFF layout (confirmed via headless decode of the real file, not
// guessed — see the chunk dump this was transcribed from):
//
//   FORM <size> "LOAD"
//     FORM <size> "WING"
//       NUM_ (u32 LE) — craft count
//       FORM <size> "0000"            (one of these per craft, in order)
//         TAGS (8 bytes, meaning not needed by the Loadout screen)
//         MAIN (nul-terminated string) — craft name
//         SUBS (nul-terminated strings, back-to-back) — spec sheet lines
//         OBJT (nul-terminated string) — in-flight model name
//         DECY (i32 LE) — decoy count
//         FORM <size> "HARD"
//           NUM_ (u32 LE) — hardpoint count
//           FORM <size> "0000"        (one of these per hardpoint, in order)
//             STAT (i32 LE, always 0 in the static file — runtime-only)
//             MASK (u32 LE) — bitwise-OR of compatible WC3WeaponDef::mask
//             MAX_ (i32 LE) — capacity
//             CORD (3x i32 LE) — absolute screen-space x,y,(unused z)
//     FORM <size> "ARMY"
//       NUM_ (u32 LE) — weapon count
//       FORM <size> "0000"            (one of these per weapon, in order)
//         MASK (u32 LE) — this weapon's own single bit
//         TAGS (8 bytes)
//         MAIN (nul-terminated string) — weapon name
//         SUBS (nul-terminated strings: "TYPE: ..." then "TACVAL: ...")
//         OBJT (nul-terminated string, absent for the two CLASSIFIED specials)
//
// Every numbered sub-FORM tag ("0000", "0001", ...) is just a positional
// index, not referenced by anything else in the file — records are read in
// file order, matching 183.SHP (craft) / 184.SHP+185.SHP (weapon) frame
// order exactly.

static uint32_t ru32be(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static int32_t ru32le(const uint8_t* p) {
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static std::string readCString(const uint8_t* p, uint32_t size) {
    const char* s = (const char*)p;
    size_t len = strnlen(s, size);
    return std::string(s, len);
}

// Splits a run of back-to-back nul-terminated strings (SUBS' actual layout)
// into individual lines.
static std::vector<std::string> splitNulStrings(const uint8_t* p, uint32_t size) {
    std::vector<std::string> lines;
    size_t i = 0;
    while (i < size) {
        size_t len = strnlen((const char*)p + i, size - i);
        if (len > 0) lines.emplace_back((const char*)p + i, len);
        i += len + 1;
    }
    return lines;
}

namespace {

// One IFF chunk header (tag+size) plus a pointer to its payload, as found
// by walking a byte range left to right — same "while(pos+8<=size){...}"
// idiom every other parser in this codebase hand-rolls.
struct ChunkView {
    std::string tag;
    const uint8_t* payload;
    uint32_t size;
};

// Returns every top-level chunk in [data, data+size), including FORM
// chunks (whose payload starts with their own 4-byte form-type tag).
std::vector<ChunkView> walkChunks(const uint8_t* data, size_t size) {
    std::vector<ChunkView> out;
    size_t pos = 0;
    while (pos + 8 <= size) {
        char tag[5] = {};
        memcpy(tag, data + pos, 4);
        uint32_t chunkSize = ru32be(data + pos + 4);
        if (pos + 8 + chunkSize > size) break;
        out.push_back({tag, data + pos + 8, chunkSize});
        pos += 8 + chunkSize + (chunkSize & 1);
    }
    return out;
}

// A FORM chunk's payload is its 4-byte type tag followed by its own nested
// chunks — this returns just the type tag (nested chunks are walked
// separately via walkChunks(c.payload + 4, c.size - 4)).
std::string formType(const ChunkView& c) {
    if (c.tag != "FORM" || c.size < 4) return "";
    return std::string((const char*)c.payload, 4);
}

bool parseHardpoint(const ChunkView& form, WC3HardpointDef& out) {
    for (auto& c : walkChunks(form.payload + 4, form.size - 4)) {
        if (c.tag == "MASK" && c.size >= 4) out.mask = (uint32_t)ru32le(c.payload);
        else if (c.tag == "MAX_" && c.size >= 4) out.capacity = ru32le(c.payload);
        else if (c.tag == "CORD" && c.size >= 8) { out.cordX = ru32le(c.payload); out.cordY = ru32le(c.payload + 4); }
    }
    return true;
}

bool parseCraft(const ChunkView& form, WC3CraftDef& out) {
    for (auto& c : walkChunks(form.payload + 4, form.size - 4)) {
        if (c.tag == "MAIN") out.name = readCString(c.payload, c.size);
        else if (c.tag == "SUBS") out.specLines = splitNulStrings(c.payload, c.size);
        else if (c.tag == "OBJT") out.objt = readCString(c.payload, c.size);
        else if (c.tag == "DECY" && c.size >= 4) out.decoyCount = ru32le(c.payload);
        else if (c.tag == "FORM" && formType(c) == "HARD") {
            for (auto& hc : walkChunks(c.payload + 4, c.size - 4)) {
                if (hc.tag == "FORM") {
                    WC3HardpointDef hp;
                    parseHardpoint(hc, hp);
                    out.hardpoints.push_back(hp);
                }
                // NUM_ (hardpoint count) is implicit in how many FORMs follow — ignored.
            }
        }
    }
    return true;
}

bool parseWeapon(const ChunkView& form, WC3WeaponDef& out) {
    for (auto& c : walkChunks(form.payload + 4, form.size - 4)) {
        if (c.tag == "MASK" && c.size >= 4) out.mask = (uint32_t)ru32le(c.payload);
        else if (c.tag == "MAIN") out.name = readCString(c.payload, c.size);
        else if (c.tag == "OBJT") out.objt = readCString(c.payload, c.size);
        else if (c.tag == "SUBS") {
            auto lines = splitNulStrings(c.payload, c.size);
            for (auto& line : lines) {
                if (line.rfind("TYPE: ", 0) == 0) out.weaponClass = line.substr(6);
                else if (line.rfind("TACVAL: ", 0) == 0) out.tacval = line.substr(8);
            }
        }
    }
    return true;
}

} // namespace

bool WC3LoadoutDatabase::loadFromBytes(const uint8_t* data, size_t size) {
    craft.clear();
    weapons.clear();
    loaded = false;
    if (size < 12 || memcmp(data, "FORM", 4) != 0) return false;
    uint32_t topSize = ru32be(data + 4);
    if (topSize < 4 || 8 + topSize > size) return false;
    if (memcmp(data + 8, "LOAD", 4) != 0) return false;

    for (auto& top : walkChunks(data + 12, topSize - 4)) {
        if (top.tag != "FORM") continue;
        std::string type = formType(top);
        if (type == "WING") {
            for (auto& c : walkChunks(top.payload + 4, top.size - 4)) {
                if (c.tag != "FORM") continue; // skip NUM_
                WC3CraftDef def;
                parseCraft(c, def);
                craft.push_back(std::move(def));
            }
        } else if (type == "ARMY") {
            for (auto& c : walkChunks(top.payload + 4, top.size - 4)) {
                if (c.tag != "FORM") continue; // skip NUM_
                WC3WeaponDef def;
                parseWeapon(c, def);
                weapons.push_back(std::move(def));
            }
        }
    }

    loaded = !craft.empty() && !weapons.empty();
    return loaded;
}

std::vector<int> WC3LoadoutDatabase::compatibleWeapons(const WC3HardpointDef& hp) const {
    std::vector<int> out;
    for (size_t i = 0; i < weapons.size(); i++) {
        if (hp.mask & weapons[i].mask) out.push_back((int)i);
    }
    return out;
}

int WC3LoadoutDatabase::firstCompatibleWeapon(const WC3HardpointDef& hp) const {
    for (size_t i = 0; i < weapons.size(); i++) {
        if (hp.mask & weapons[i].mask) return (int)i;
    }
    return 0;
}

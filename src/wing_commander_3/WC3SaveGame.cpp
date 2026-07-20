#include "WC3SaveGame.h"
#include "../commons/IFFWriter.h"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <algorithm>
#include <functional>

static uint32_t ru32be(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint32_t ru32le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Bank slots confirmed to matter beyond a single mission cycle and NOT
// already re-derived deterministically elsewhere:
//   A-12: "was the just-completed mission successful" — read by multiple
//         wingman reaction-movie gumps (see WC3Scene::setBankA's comment).
// Extend this allowlist as more slots are identified; everything else
// (e.g. roster-availability 143-149) is recomputed fresh by loadScene().
static constexpr int kSavedBankASlots[] = {12};
static constexpr int kSavedBankBSlots[] = {155, 191};

std::string saveSlotPath(int slotNumber) {
    char buf[64];
    snprintf(buf, sizeof(buf), "saves/wc3save%d.dat", slotNumber);
    return buf;
}

static void ensureSavesDir() {
    struct stat st;
    if (stat("saves", &st) != 0) {
#if defined(_WIN32)
        system("mkdir saves");
#else
        mkdir("saves", 0755);
#endif
    }
}

bool writeSaveGame(int slotNumber, const WC3SaveGame& in) {
    ensureSavesDir();
    IFFWriter w;
    w.StartIFF("WCSV");

    w.StartChunk("HEAD");
    w.WriteInt32(in.currentScene);
    char roomId[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < 4 && i < in.roomId.size(); i++) roomId[i] = in.roomId[i];
    w.WriteData(roomId, 4);
    w.EndChunk();

    w.StartChunk("NAME");
    w.WriteData(in.name.data(), (uint32_t)in.name.size());
    w.EndChunk();

    w.StartChunk("VARB");
    for (int slot : kSavedBankASlots) {
        auto it = in.bankA.find(slot);
        if (it == in.bankA.end()) continue;
        w.WriteUint8('A');
        w.WriteInt32(slot);
        w.WriteInt32(it->second);
    }
    for (int slot : kSavedBankBSlots) {
        auto it = in.bankB.find(slot);
        if (it == in.bankB.end()) continue;
        w.WriteUint8('B');
        w.WriteInt32(slot);
        w.WriteInt32(it->second);
    }
    w.EndChunk();

    w.StartChunk("ROST");
    w.WriteUint32((uint32_t)in.pilots.size());
    for (auto& p : in.pilots) {
        uint8_t nameLen = (uint8_t)std::min<size_t>(p.name.size(), 255);
        w.WriteUint8(nameLen);
        w.WriteData(p.name.data(), nameLen);
        w.WriteInt32(p.aces);
        w.WriteInt32(p.kills);
    }
    w.EndChunk();

    w.StartChunk("LOAD");
    w.WriteInt32(in.loadoutShipIndex);
    w.WriteUint32((uint32_t)in.loadoutHardpoints.size());
    for (auto& hp : in.loadoutHardpoints) {
        w.WriteInt32(hp.missileTypeIndex);
        w.WriteInt32(hp.loadedCount);
    }
    w.EndChunk();

    w.StartChunk("MRAL");
    w.WriteInt32(in.shipMorale);
    for (size_t i = 0; i < 8; i++) {
        w.WriteInt32(in.pilotMorale[i]);
        w.WriteUint8(in.pilotRosterOverride[i]);
        w.WriteInt32(in.pilotRelationshipStatus[i]);
    }
    w.EndChunk();

    return w.SaveToFile(saveSlotPath(slotNumber).c_str());
}

// Reads a whole slot file into memory and returns it (empty on failure).
static std::vector<uint8_t> readWholeFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> data(size > 0 ? (size_t)size : 0);
    if (size > 0) {
        if (fread(data.data(), 1, (size_t)size, f) != (size_t)size) data.clear();
    }
    fclose(f);
    return data;
}

// Shared chunk-walk for both the cheap header-only peek and the full load —
// same hand-rolled "while(pos+8<=size){tag;size;...}" idiom every other IFF
// consumer in this codebase already uses (WC3Scene::parseGump, etc.).
static void forEachChunk(const std::vector<uint8_t>& data,
                          const std::function<bool(const char* tag, const uint8_t* payload, uint32_t size)>& fn) {
    if (data.size() < 12 || memcmp(data.data(), "FORM", 4) != 0) return;
    uint32_t formSize = ru32be(&data[4]);
    size_t end = std::min(data.size(), (size_t)formSize + 8);
    size_t pos = 12; // skip FORM + size + "WCSV"
    while (pos + 8 <= end) {
        char tag[5] = {};
        memcpy(tag, &data[pos], 4);
        uint32_t sz = ru32be(&data[pos + 4]);
        if (pos + 8 + sz > end) break;
        if (!fn(tag, &data[pos + 8], sz)) return;
        pos += 8 + sz + (sz & 1);
    }
}

bool peekSaveGameHeader(int slotNumber, int& outScene, std::string& outRoomId, std::string& outName) {
    std::vector<uint8_t> data = readWholeFile(saveSlotPath(slotNumber));
    if (data.empty()) return false;
    bool found = false;
    outName.clear();
    forEachChunk(data, [&](const char* tag, const uint8_t* p, uint32_t sz) {
        if (strcmp(tag, "HEAD") == 0 && sz >= 8) {
            outScene = (int)ru32le(p);
            outRoomId.assign((const char*)p + 4, 4);
            found = true;
            return true; // NAME (if present) directly follows HEAD — keep scanning
        }
        if (strcmp(tag, "NAME") == 0) {
            outName.assign((const char*)p, sz);
            return false; // nothing else this peek needs
        }
        return true;
    });
    return found;
}

bool loadSaveGame(int slotNumber, WC3SaveGame& out) {
    std::vector<uint8_t> data = readWholeFile(saveSlotPath(slotNumber));
    if (data.empty()) return false;
    out = WC3SaveGame{};
    bool foundHead = false;
    forEachChunk(data, [&](const char* tag, const uint8_t* p, uint32_t sz) {
        if (strcmp(tag, "HEAD") == 0 && sz >= 8) {
            out.currentScene = (int)ru32le(p);
            out.roomId.assign((const char*)p + 4, 4);
            foundHead = true;
        } else if (strcmp(tag, "NAME") == 0) {
            out.name.assign((const char*)p, sz);
        } else if (strcmp(tag, "VARB") == 0) {
            size_t i = 0;
            while (i + 9 <= sz) {
                char bank = (char)p[i];
                int32_t slot = (int32_t)ru32le(p + i + 1);
                int32_t value = (int32_t)ru32le(p + i + 5);
                if (bank == 'A') out.bankA[slot] = value;
                else if (bank == 'B') out.bankB[slot] = value;
                i += 9;
            }
        } else if (strcmp(tag, "ROST") == 0 && sz >= 4) {
            uint32_t count = ru32le(p);
            size_t i = 4;
            for (uint32_t r = 0; r < count && i < sz; r++) {
                if (i + 1 > sz) break;
                uint8_t nameLen = p[i]; i += 1;
                if (i + nameLen + 8 > sz) break;
                WC3PilotStats stats;
                stats.name.assign((const char*)p + i, nameLen); i += nameLen;
                stats.aces = (int)ru32le(p + i); i += 4;
                stats.kills = (int)ru32le(p + i); i += 4;
                out.pilots.push_back(std::move(stats));
            }
        } else if (strcmp(tag, "LOAD") == 0 && sz >= 8) {
            out.loadoutShipIndex = (int)ru32le(p);
            uint32_t hpCount = ru32le(p + 4);
            size_t i = 8;
            for (uint32_t h = 0; h < hpCount && i + 8 <= sz; h++) {
                WC3LoadoutHardpointState hp;
                hp.missileTypeIndex = (int)ru32le(p + i); i += 4;
                hp.loadedCount = (int)ru32le(p + i); i += 4;
                out.loadoutHardpoints.push_back(hp);
            }
        } else if (strcmp(tag, "MRAL") == 0 && sz >= 4) {
            out.shipMorale = (int)ru32le(p);
            size_t i = 4;
            // 9 bytes/record (i32 morale + u8 override + i32 relationship
            // status). A short/legacy record just stops here and leaves the
            // rest at their zero defaults, same tolerance as ROST/LOAD above.
            for (size_t pi = 0; pi < 8 && i + 9 <= sz; pi++) {
                out.pilotMorale[pi] = (int)ru32le(p + i); i += 4;
                out.pilotRosterOverride[pi] = p[i]; i += 1;
                out.pilotRelationshipStatus[pi] = (int)ru32le(p + i); i += 4;
            }
        }
        return true;
    });
    return foundHead;
}

#include "WC3Globals.h"
#include "../realspace/RSWC3Shape.h"
#include <cstring>

uint32_t WC3Globals::r32be(const uint8_t* p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
uint32_t WC3Globals::r32le(const uint8_t* p) {
    return p[0]|(p[1]<<8)|(p[2]<<16)|((uint32_t)p[3]<<24);
}

bool WC3Globals::load(const uint8_t* data, size_t size) {
    if (size < 12 || memcmp(data, "FORM", 4) != 0 || memcmp(data+8, "GLOB", 4) != 0)
        return false;

    uint32_t globSz = r32be(data + 4);
    size_t end = std::min(size, (size_t)globSz + 8);

    size_t pos = 12;
    while (pos + 8 <= end) {
        uint32_t csz = r32be(data + pos + 4);
        size_t   cend = pos + 8 + csz;
        if (cend > end) break;

        if (memcmp(data+pos, "FORM", 4) == 0 && csz >= 4) {
            const uint8_t* sub = data + pos + 8;
            const uint8_t* subData = data + pos + 12;
            size_t subSz = csz - 4;
            if      (memcmp(sub, "PATH", 4) == 0) parsePath(subData, subSz);
            else if (memcmp(sub, "FONT", 4) == 0) parseFont(subData, subSz);
            else if (memcmp(sub, "WRLD", 4) == 0) parseWrld(subData, subSz);
            else if (memcmp(sub, "PNTR", 4) == 0) parsePntr(subData, subSz);
        }

        pos = cend + (csz & 1);
    }

    loaded = true;
    printf("WC3Globals: loaded — %zu paths, %zu fonts, cursors=%s\n",
           paths.size(), fonts.size(), cursorSet ? "yes" : "no");
    return true;
}

// ---------------------------------------------------------------------------
// FORM:PATH — 16 tag→path entries, each an IFF chunk: TAG size path_string
// ---------------------------------------------------------------------------
void WC3Globals::parsePath(const uint8_t* data, size_t size) {
    size_t pos = 0;
    while (pos + 8 <= size) {
        char   tag[5] = {};
        memcpy(tag, data + pos, 4);
        uint32_t csz = r32be(data + pos + 4);
        if (pos + 8 + csz > size) break;
        std::string path(reinterpret_cast<const char*>(data + pos + 8),
                         strnlen(reinterpret_cast<const char*>(data + pos + 8), csz));
        paths[tag] = path;
        pos += 8 + csz + (csz & 1);
    }
}

// ---------------------------------------------------------------------------
// FORM:FONT — INFO chunk + FORM:DATA containing 9 font chunks
// ---------------------------------------------------------------------------
void WC3Globals::parseFont(const uint8_t* data, size_t size) {
    size_t pos = 0;
    while (pos + 8 <= size) {
        uint32_t csz = r32be(data + pos + 4);
        size_t   cend = pos + 8 + csz;
        if (cend > size) break;

        if (memcmp(data+pos, "FORM", 4) == 0 && csz >= 4 &&
            memcmp(data+pos+8, "DATA", 4) == 0) {
            // Walk the 9 font sub-chunks
            size_t j = pos + 12;
            while (j + 8 <= cend) {
                char   name[5] = {};
                memcpy(name, data + j, 4);
                uint32_t fsz = r32be(data + j + 4);
                size_t   fend = j + 8 + fsz;
                if (fend > cend) break;

                WC3Font* font = new WC3Font();
                if (font->load(data + j + 8, fsz)) {
                    fonts[name] = font;
                } else {
                    delete font;
                    printf("WC3Globals: failed to parse font %s\n", name);
                }
                j = fend + (fsz & 1);
            }
        }

        pos = cend + (csz & 1);
    }
}

// ---------------------------------------------------------------------------
// FORM:WRLD — TICK chunk: 16.16 fixed-point ticks/sec
// ---------------------------------------------------------------------------
void WC3Globals::parseWrld(const uint8_t* data, size_t size) {
    size_t pos = 0;
    while (pos + 8 <= size) {
        uint32_t csz = r32be(data + pos + 4);
        if (pos + 8 + csz > size) break;
        if (memcmp(data+pos, "TICK", 4) == 0 && csz >= 4) {
            uint32_t raw = r32le(data + pos + 8);
            tickRate = (float)(raw >> 16) + (float)(raw & 0xFFFF) / 65536.0f;
        }
        pos += 8 + csz + (csz & 1);
    }
}

// ---------------------------------------------------------------------------
// FORM:PNTR — SHAP chunk: 1.11 cursor shape bundle (10 cursors, 30×31 px)
// ---------------------------------------------------------------------------
void WC3Globals::parsePntr(const uint8_t* data, size_t size) {
    size_t pos = 0;
    while (pos + 8 <= size) {
        uint32_t csz = r32be(data + pos + 4);
        if (pos + 8 + csz > size) break;
        if (memcmp(data+pos, "SHAP", 4) == 0) {
            // Not a PakArchive (that decoder was silently failing here —
            // "not a PAK archive", 0 cursors loaded, always falling back to
            // the hardcoded 12x12 arrow). Like WC3's other SHAP-named
            // chunks (cockpit art, WRLD's STAR glints), this is the WC3
            // "1.1x" shape-pak format instead — a single entry (marker at
            // offset 0, no header to skip), 10 cursor frames.
            RSImageSet* cursors = RSWC3DecodeShapeEntry(data + pos + 8, csz);
            cursorSet = cursors;
            printf("WC3Globals: loaded %zu cursor(s)\n", cursorSet->GetNumImages());
            return;
        }
        pos += 8 + csz + (csz & 1);
    }
}

// ---------------------------------------------------------------------------
std::string WC3Globals::getPath(const char* tag) const {
    auto it = paths.find(tag);
    return it != paths.end() ? it->second : "";
}

WC3Font* WC3Globals::getFont(const char* name) const {
    auto it = fonts.find(name);
    return it != fonts.end() ? it->second : nullptr;
}

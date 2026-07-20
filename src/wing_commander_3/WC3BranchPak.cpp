#include "WC3BranchPak.h"
#include "../commons/XtreDecompressor.h"
#include <SDL2/SDL.h>
#include <cstring>

uint32_t WC3BranchPak::u32le(const uint8_t* p) {
    return p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24);
}
uint32_t WC3BranchPak::u32be(const uint8_t* p) {
    return (p[0]<<24) | (p[1]<<16) | (p[2]<<8) | p[3];
}

// ---------------------------------------------------------------------------
// Type 0x40: XtreDecompressor Type1 with 4-byte LE uncompressed-size prefix
// ---------------------------------------------------------------------------
bool WC3BranchPak::decompressType40(const uint8_t* src, size_t srcSize,
                                    std::vector<uint8_t>& out) {
    if (srcSize < 4) return false;
    uint32_t uncmprSz = u32le(src);
    if (uncmprSz == 0 || uncmprSz > 8 * 1024 * 1024) return false;
    out.resize(uncmprSz);
    size_t got = XtreDecompressor::decompressType1(src + 4, out.data(), srcSize - 4);
    out.resize(got);
    return got > 0;
}

// ---------------------------------------------------------------------------
// Parse a single FORM TRUE or FORM FALS block (TEXT + SOND children) into a line.
// ---------------------------------------------------------------------------
bool WC3BranchPak::parseBranchLine(const uint8_t* data, size_t size, WC3BranchLine& out) {
    uint32_t formSz = u32be(data + 4);
    size_t end = std::min(size, (size_t)formSz + 8);

    size_t pos = 12;
    while (pos + 8 <= end) {
        uint32_t csz = u32be(data + pos + 4);
        size_t cend = pos + 8 + csz;
        if (cend > end) break;

        if (memcmp(data + pos, "FORM", 4) == 0 && csz >= 4) {
            // Walk sub-chunks of this FORM (TEXT / SOND / etc.)
            size_t j = pos + 12;
            size_t je = cend;
            while (j + 8 <= je) {
                uint32_t ss = u32be(data + j + 4);
                size_t   se = j + 8 + ss;
                if (se > je) break;

                if (memcmp(data + j, "ENG_", 4) == 0 && ss > 0) {
                    const char* txt = reinterpret_cast<const char*>(data + j + 8);
                    size_t len = strnlen(txt, ss);
                    out.text_eng.assign(txt, len);
                } else if (memcmp(data + j, "FRE_", 4) == 0 && ss > 0) {
                    const char* txt = reinterpret_cast<const char*>(data + j + 8);
                    size_t len = strnlen(txt, ss);
                    out.text_fre.assign(txt, len);
                } else if (memcmp(data + j, "GER_", 4) == 0 && ss > 0) {
                    const char* txt = reinterpret_cast<const char*>(data + j + 8);
                    size_t len = strnlen(txt, ss);
                    out.text_ger.assign(txt, len);
                } else if (memcmp(data + j, "DIGI", 4) == 0 && ss > 0) {
                    out.audio.assign(data + j + 8, data + j + 8 + ss);
                }

                j = se;
                if (ss & 1) j++;
            }
        }

        pos = cend;
        if (csz & 1) pos++;
    }

    return !out.isEmpty();
}

// ---------------------------------------------------------------------------
// Parse an entry's two sibling top-level containers: FORM TRUE then FORM FALS.
// ---------------------------------------------------------------------------
bool WC3BranchPak::parseEntry(const uint8_t* data, size_t size, WC3BranchEntry& out) {
    size_t pos = 0;
    bool gotAny = false;
    while (pos + 12 <= size) {
        if (memcmp(data + pos, "FORM", 4) != 0) break;
        uint32_t formSz = u32be(data + pos + 4);
        size_t cend = pos + 8 + formSz;
        if (cend > size) cend = size;

        if (memcmp(data + pos + 8, "TRUE", 4) == 0)
            gotAny |= parseBranchLine(data + pos, cend - pos, out.true_line);
        else if (memcmp(data + pos + 8, "FALS", 4) == 0)
            gotAny |= parseBranchLine(data + pos, cend - pos, out.false_line);

        pos = cend;
        if (formSz & 1) pos++;
    }
    return gotAny;
}

// ---------------------------------------------------------------------------
// Load PAK
// ---------------------------------------------------------------------------
bool WC3BranchPak::load(const uint8_t* data, size_t size) {
    entries.clear();
    if (size < 8) return false;

    // First 4 bytes = advertised file size
    uint32_t advSz = u32le(data);
    if (advSz != (uint32_t)size) {
        printf("WC3BranchPak: size mismatch advertised=%u actual=%zu\n", advSz, size);
        return false;
    }

    // Entry 0's offset gives us numEntries = (entry0_offset - 4) / 4
    uint32_t e0raw = u32le(data + 4);
    uint32_t off0  = e0raw & 0x00FFFFFF;
    if (off0 < 8 || off0 > size) return false;
    uint32_t numEntries = (off0 - 4) / 4;

    for (uint32_t i = 0; i < numEntries; i++) {
        uint32_t raw  = u32le(data + 4 + i * 4);
        uint8_t  type = (raw >> 24) & 0xFF;
        uint32_t off  = raw & 0x00FFFFFF;

        // Compute entry size
        uint32_t nextOff;
        if (i + 1 < numEntries)
            nextOff = u32le(data + 4 + (i + 1) * 4) & 0x00FFFFFF;
        else
            nextOff = (uint32_t)size;

        if (off >= size || nextOff <= off) {
            entries.push_back({});  // empty placeholder
            continue;
        }

        const uint8_t* src = data + off;
        size_t srcSz = nextOff - off;

        std::vector<uint8_t> decoded;
        const uint8_t* entryData = src;
        size_t         entrySize = srcSz;

        if (type == 0x40) {
            if (!decompressType40(src, srcSz, decoded)) {
                entries.push_back({});
                continue;
            }
            entryData = decoded.data();
            entrySize = decoded.size();
        } else if (type == 0xFF || srcSz == 0) {
            entries.push_back({});
            continue;
        }

        WC3BranchEntry entry;
        if (!parseEntry(entryData, entrySize, entry))
            printf("WC3BranchPak: entry %u parse failed (type=%02x sz=%zu)\n", i, type, srcSz);

        entries.push_back(std::move(entry));
    }

    printf("WC3BranchPak: loaded %zu entries\n", entries.size());
    return !entries.empty();
}

const WC3BranchEntry* WC3BranchPak::getEntry(int index) const {
    if (index < 0 || index >= (int)entries.size()) return nullptr;
    return &entries[index];
}

// ---------------------------------------------------------------------------
// Audio playback (blocking)
// ---------------------------------------------------------------------------
void WC3BranchPak::playEntry(int index, bool playFalse) const {
    const WC3BranchEntry* e = getEntry(index);
    if (!e) return;
    const WC3BranchLine& line = playFalse ? e->false_line : e->true_line;

    if (!line.text_eng.empty())
        printf("Blair: \"%s\"\n", line.text_eng.c_str());

    if (line.audio.empty()) return;

    SDL_AudioDeviceID dev = 0;
    SDL_AudioSpec want{}, got{};
    want.freq     = 22050;
    want.format   = AUDIO_S16LSB;
    want.channels = 1;
    want.samples  = 512;
    dev = SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);
    if (!dev) {
        printf("WC3BranchPak: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return;
    }

    SDL_QueueAudio(dev, line.audio.data(), (uint32_t)line.audio.size());
    SDL_PauseAudioDevice(dev, 0);

    // Wait until audio drains (or ESC pressed)
    while (SDL_GetQueuedAudioSize(dev) > 0) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT ||
               (ev.type == SDL_KEYDOWN && ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE)) {
                SDL_ClearQueuedAudio(dev);
                goto done;
            }
        }
        SDL_Delay(16);
    }

done:
    SDL_CloseAudioDevice(dev);
}

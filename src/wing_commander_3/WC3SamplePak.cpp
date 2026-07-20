#include "WC3SamplePak.h"
#include <cstring>
#include <cstdio>
#include <algorithm>

uint32_t WC3SamplePak::u32le(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint16_t WC3SamplePak::u16le(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

bool WC3SamplePak::load(const uint8_t* data, size_t size) {
    rawSamples.clear();
    wavCache.clear();
    if (size < 12 || memcmp(data, "FORM", 4) != 0) return false;

    // FORM SOND > FORM DIGI > SOND — walk down to the SOND chunk's payload,
    // which holds the back-to-back [index][marker][size]<PCM...> records.
    if (memcmp(data + 8, "SOND", 4) != 0) return false;
    size_t pos = 12;
    if (pos + 12 > size || memcmp(data + pos, "FORM", 4) != 0) return false;
    if (memcmp(data + pos + 8, "DIGI", 4) != 0) return false;
    pos += 12;
    if (pos + 8 > size || memcmp(data + pos, "SOND", 4) != 0) return false;
    uint32_t sondSize = (data[pos+4]<<24)|(data[pos+5]<<16)|(data[pos+6]<<8)|data[pos+7]; // big-endian IFF size
    size_t payloadStart = pos + 8;
    size_t payloadEnd = std::min(size, payloadStart + (size_t)sondSize);

    ownedData.assign(data, data + size);
    const uint8_t* d = ownedData.data();

    size_t p = payloadStart;
    while (p + 8 <= payloadEnd) {
        uint16_t index = u16le(d + p);
        uint16_t marker = u16le(d + p + 2);
        uint32_t sampleSize = u32le(d + p + 4);
        p += 8;
        // 0xEAEA: GFSAMPLE.IFF. 0xDEAF: SOUND/SAMPLES.IFF -- same container/
        // record shape, different marker constant (confirmed empirically,
        // walking SAMPLES.IFF end-to-end with this accepted cleanly decodes
        // 44 consistent records with plausible sizes).
        if ((marker != 0xEAEA && marker != 0xDEAF) || p + sampleSize > payloadEnd) {
            printf("WC3SamplePak: bad record marker %04x at sample %u, stopping scan\n", marker, index);
            break;
        }
        rawSamples[(int)index] = { p, sampleSize };
        p += sampleSize;
    }

    printf("WC3SamplePak: loaded %zu samples\n", rawSamples.size());
    return !rawSamples.empty();
}

const std::vector<uint8_t>* WC3SamplePak::getSampleWav(int index) {
    auto cached = wavCache.find(index);
    if (cached != wavCache.end()) return &cached->second;

    auto it = rawSamples.find(index);
    if (it == rawSamples.end()) return nullptr;

    const uint8_t* pcm = ownedData.data() + it->second.offset;
    uint32_t pcmSize = (uint32_t)it->second.size;

    std::vector<uint8_t> wav;
    wav.reserve(44 + pcmSize);
    auto push32 = [&](uint32_t v) {
        wav.push_back((uint8_t)(v & 0xFF)); wav.push_back((uint8_t)((v >> 8) & 0xFF));
        wav.push_back((uint8_t)((v >> 16) & 0xFF)); wav.push_back((uint8_t)((v >> 24) & 0xFF));
    };
    auto push16 = [&](uint16_t v) {
        wav.push_back((uint8_t)(v & 0xFF)); wav.push_back((uint8_t)((v >> 8) & 0xFF));
    };
    auto pushStr = [&](const char* s) { wav.insert(wav.end(), s, s + 4); };

    const uint16_t channels = 1;
    const uint32_t byteRate = this->sampleRate * channels * this->bitsPerSample / 8;
    const uint16_t blockAlign = channels * this->bitsPerSample / 8;

    pushStr("RIFF"); push32(36 + pcmSize); pushStr("WAVE");
    pushStr("fmt "); push32(16); push16(1) /* PCM */; push16(channels);
    push32(this->sampleRate); push32(byteRate); push16(blockAlign); push16(this->bitsPerSample);
    pushStr("data"); push32(pcmSize);
    wav.insert(wav.end(), pcm, pcm + pcmSize);

    auto& slot = wavCache[index];
    slot = std::move(wav);
    return &slot;
}

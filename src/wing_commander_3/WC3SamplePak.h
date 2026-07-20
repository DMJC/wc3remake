#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <unordered_map>

// GAMEFLOW/SOUND/GFSAMPLE.IFF (same container/record format as SAMPLES.IFF
// from missions.tre, unverified): a sequential archive of raw digital
// sound-effect samples, referenced by index from a gump's own SOND/_ID_
// chunk (see WC3Gump::sound_ids).
//
// Container: FORM SOND > FORM DIGI > SOND, whose payload is a back-to-back
// sequence of records:
//   [index:u16LE][marker:u16LE = 0xEAEA][byteSize:u32LE]
//   <byteSize bytes of raw signed 16-bit little-endian mono PCM audio at
//   22050 Hz — the same raw format used elsewhere for WC3 digital audio
//   (MVE AUDI chunks, BRANCH pak voice lines; see WC3BranchPak.h) — no
//   VOC/WAV header of its own>.
// Reverse-engineered empirically: walking GFSAMPLE.IFF with this record
// shape consumes the file exactly to EOF with zero slack across all ~33
// records, and every sample index referenced by gump SOND/_ID_ chunks across
// VICTORY.IFF falls inside the decoded index range.
class WC3SamplePak {
public:
    // Defaults match GFSAMPLE.IFF's own raw PCM format (16-bit signed,
    // 22050 Hz). SOUND/SAMPLES.IFF (same container/record shape, confirmed)
    // is actually 8-bit unsigned PCM at 11025 Hz -- pass those explicitly
    // for that file. Both were empirically confirmed (waveform shape /
    // listening test), not from any documented spec.
    WC3SamplePak(uint32_t sampleRate = 22050, uint16_t bitsPerSample = 16)
        : sampleRate(sampleRate), bitsPerSample(bitsPerSample) {}

    bool load(const uint8_t* data, size_t size);

    // Returns a ready-to-play WAV file (44-byte header + raw PCM) for the
    // given sample index, built and cached on first request. nullptr if
    // index isn't present in the archive.
    const std::vector<uint8_t>* getSampleWav(int index);

private:
    struct RawSample { size_t offset; size_t size; };
    std::unordered_map<int, RawSample> rawSamples;
    std::unordered_map<int, std::vector<uint8_t>> wavCache;
    std::vector<uint8_t> ownedData;
    uint32_t sampleRate;
    uint16_t bitsPerSample;

    static uint32_t u32le(const uint8_t* p);
    static uint16_t u16le(const uint8_t* p);
};

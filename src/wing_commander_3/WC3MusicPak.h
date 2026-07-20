#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <unordered_map>

// Loads GAMEFLOW.TRE's SOUND/GMMUSIC.IFF — General MIDI background music for
// gameflow scenes.
//
// File shape: FORM SOND > FORM MIDI > SOND, whose payload is a back-to-back
// sequence of per-track records, each an 8-byte header
// [trackIndex:u32LE][byteSize:u32LE] followed by that many bytes of a
// proprietary HMI Sound Operating System "HMIMIDIP" MIDI stream (the same
// format documented/converted by https://github.com/NRS-NewRisingSun/hmi2mid,
// used by several early-90s Origin titles). Confirmed byte-exact against every
// record boundary in the real GMMUSIC.IFF (33 tracks).
//
// getTrackMidi() converts a track to a Standard MIDI File byte stream on first
// use (C++ port of the hmi2mid algorithm above, validated against real WC3
// data via Python prototype + `mido` semantic verification before porting),
// suitable for any standard MIDI player (SDL_mixer's MUS_MID/MUS_ADLMIDI).
class WC3MusicPak {
public:
    bool load(const uint8_t* data, size_t size);

    // Returns Standard MIDI bytes for gameflow track `index`, or nullptr if
    // not found / conversion failed. Converted once, then cached.
    const std::vector<uint8_t>* getTrackMidi(int index);

private:
    struct RawTrack { size_t offset{0}; size_t size{0}; };
    std::unordered_map<int, RawTrack> rawTracks;
    std::unordered_map<int, std::vector<uint8_t>> midiCache;
    std::vector<uint8_t> ownedData; // keeps raw HMI bytes alive for rawTracks' offsets

    static uint32_t u32le(const uint8_t* p);
    // Converts one HMIMIDIP track blob to a Standard MIDI File. Returns false
    // if the blob isn't recognized as HMIMIDI or is malformed.
    static bool convertHmiToMidi(const uint8_t* hmi, size_t hmiSize, std::vector<uint8_t>& out);
};

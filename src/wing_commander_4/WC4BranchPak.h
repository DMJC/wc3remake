#pragma once
#include "../strike_commander/precomp.h"

struct WC4BranchEntry {
    std::string text_eng;
    std::string text_fre;
    std::string text_ger;
    // Raw signed 16-bit LE PCM, 22050 Hz mono (same format as MVE AUDI chunks)
    std::vector<uint8_t> audio;
};

// Loads a GAMEFLOW/BRANCHn.PAK — the player-dialogue (Blair's voice lines) archive.
//
// PAK format: 4-byte LE file size, then entry table (high byte = type, low 3 bytes = data offset).
// Entry type 0x00 = raw FORM TRUE IFF.
// Entry type 0x40 = XtreDecompressor Type1 compressed with 4-byte LE uncompressed-size prefix.
// Entry type 0xFF = empty slot.
//
// Each FORM TRUE contains:
//   FORM (type) → FRE_/GER_/ENG_ IFF chunks (null-terminated localised text)
//   FORM SOND   → _ID_ (4 bytes) + DIGI (raw S16LE PCM audio)
class WC4BranchPak {
public:
    bool load(const uint8_t* data, size_t size);

    int  getEntryCount() const { return (int)entries.size(); }
    bool isEmpty()       const { return entries.empty(); }
    const WC4BranchEntry* getEntry(int index) const;

    // Play the audio for entry K and print the English subtitle to stdout.
    // Blocks until audio has finished (or ESC is pressed).
    void playEntry(int index) const;

private:
    std::vector<WC4BranchEntry> entries;

    static bool        decompressType40(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& out);
    static bool        parseFormTrue(const uint8_t* data, size_t size, WC4BranchEntry& out);
    static uint32_t    u32le(const uint8_t* p);
    static uint32_t    u32be(const uint8_t* p);
};

#pragma once
#include "../strike_commander/precomp.h"

// One localised text + audio line (either the TRUE-branch or FALS-branch response).
struct WC3BranchLine {
    std::string text_eng;
    std::string text_fre;
    std::string text_ger;
    // Raw signed 16-bit LE PCM, 22050 Hz mono (same format as MVE AUDI chunks)
    std::vector<uint8_t> audio;

    bool isEmpty() const { return text_eng.empty() && audio.empty(); }
};

// A BRANCHn.PAK entry packs two sibling lines — a FORM TRUE and a FORM FALS —
// for the same conversation slot. Which one the game plays depends on some
// runtime condition that hasn't been identified yet; callers currently default
// to the TRUE line (matching this class's pre-existing behavior).
struct WC3BranchEntry {
    WC3BranchLine true_line;
    WC3BranchLine false_line;
};

// Loads a GAMEFLOW/BRANCHn.PAK — the player-dialogue (Blair's voice lines) archive.
//
// PAK format: 4-byte LE file size, then entry table (high byte = type, low 3 bytes = data offset).
// Entry type 0x00 = raw FORM TRUE IFF.
// Entry type 0x40 = XtreDecompressor Type1 compressed with 4-byte LE uncompressed-size prefix.
// Entry type 0xFF = empty slot.
//
// Each decompressed entry contains two sibling top-level FORMs — FORM TRUE and FORM FALS —
// each shaped:
//   FORM (type) → FRE_/GER_/ENG_ IFF chunks (null-terminated localised text)
//   FORM SOND   → _ID_ (4 bytes) + DIGI (raw S16LE PCM audio)
class WC3BranchPak {
public:
    bool load(const uint8_t* data, size_t size);

    int  getEntryCount() const { return (int)entries.size(); }
    bool isEmpty()       const { return entries.empty(); }
    const WC3BranchEntry* getEntry(int index) const;

    // Play the audio for entry K and print the English subtitle to stdout.
    // Blocks until audio has finished (or ESC is pressed).
    // playFalse selects the FALS-branch line instead of the default TRUE-branch line.
    void playEntry(int index, bool playFalse = false) const;

private:
    std::vector<WC3BranchEntry> entries;

    static bool        decompressType40(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& out);
    static bool        parseBranchLine(const uint8_t* data, size_t size, WC3BranchLine& out);
    static bool        parseEntry(const uint8_t* data, size_t size, WC3BranchEntry& out);
    static uint32_t    u32le(const uint8_t* p);
    static uint32_t    u32be(const uint8_t* p);
};

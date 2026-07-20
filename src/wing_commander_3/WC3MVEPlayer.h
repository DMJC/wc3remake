#pragma once
#include <cstdint>
#include <cstddef>

class WC3MVEPlayer {
public:
    // Decode and display an MVE movie from in-memory data.
    // Returns when the movie ends or the user presses ESC/closes the window.
    static void play(const uint8_t* data, size_t size);

    // Play a single shot (scene) from a multi-shot MVE.
    // shotIndex 0 = first shot. No-op if shotIndex is out of range.
    static void playShot(const uint8_t* data, size_t size, int shotIndex);

    // Play an entire BRCH group (as listed in the file's INDX chunk) — every raw
    // SHOT-delimited segment from that branch's start up to the next branch (or
    // end of file), back to back as one continuous clip. Branching conversation
    // movies (used with a VAR(49) branch-pak choice) pack their intro/true-
    // reaction/false-reaction as one BRCH group each, and a single narrative
    // "shot" as the game's bytecode means it can itself contain multiple raw
    // SHOT (palette-slot-select) chunks — e.g. camera cuts within the same
    // take — so playShot()'s single-raw-SHOT granularity cuts these off early.
    // Falls back to playShot(groupIndex) if the file has no INDX chunk.
    static void playBranchGroup(const uint8_t* data, size_t size, int groupIndex);
};

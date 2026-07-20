#pragma once
#include <cstdint>
#include <cstddef>

class WC4MVEPlayer {
public:
    // Decode and display an MVE movie from in-memory data.
    // Returns when the movie ends or the user presses ESC/closes the window.
    static void play(const uint8_t* data, size_t size);

    // Play a single shot (scene) from a multi-shot MVE.
    // shotIndex 0 = first shot. No-op if shotIndex is out of range.
    static void playShot(const uint8_t* data, size_t size, int shotIndex);
};

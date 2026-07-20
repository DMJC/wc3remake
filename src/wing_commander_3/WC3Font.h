#pragma once
#include "../strike_commander/precomp.h"
#include "../engine/FrameBuffer.h"

class WC3Font {
public:
    bool load(const uint8_t* data, size_t size);

    int getHeight() const { return glyphHeight; }
    int getNumChars() const { return numChars; }
    bool isLoaded() const { return !glyphs.empty(); }

    // Draw text at (x, y); returns x-advance (text width in pixels).
    // Palette indices from the font are written directly — render after
    // the room palette has been loaded so colours map correctly.
    int drawText(FrameBuffer* fb, const std::string& text, int x, int y) const;

    // Like drawText but overrides every non-transparent pixel with colorIdx.
    int drawTextColored(FrameBuffer* fb, const std::string& text, int x, int y,
                        uint8_t colorIdx) const;

    // Draw text directly into an RGBA buffer (pre-flip, screen coords).
    // color is RGBA in byte order: r | (g<<8) | (b<<16) | (a<<24).
    int drawTextRGBA(uint32_t* rgba, int fbW, int fbH,
                     const std::string& text, int x, int y,
                     uint32_t color = 0xFFFFFFFF) const;

    // Measure text width without drawing.
    int measureText(const std::string& text) const;

private:
    struct Glyph {
        int width{0};
        std::vector<uint8_t> pixels; // row-major, width * height palette indices
    };

    int numChars{0};
    int glyphHeight{0};
    int transparentIndex{255};
    std::vector<Glyph> glyphs;

    static uint32_t r32le(const uint8_t* p);
};

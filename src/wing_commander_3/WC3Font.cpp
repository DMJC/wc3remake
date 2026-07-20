#include "WC3Font.h"
#include <cstring>

uint32_t WC3Font::r32le(const uint8_t* p) {
    return p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24);
}

bool WC3Font::load(const uint8_t* data, size_t size) {
    glyphs.clear();
    if (size < 16) return false;
    if (memcmp(data, "1.\x00\x00", 4) != 0) return false;

    numChars        = (int)r32le(data + 4);
    glyphHeight     = (int)r32le(data + 8);
    transparentIndex = (int)r32le(data + 12);

    size_t tableEnd = 16 + (size_t)numChars * 4;
    if (tableEnd > size) return false;

    glyphs.resize(numChars);
    for (int i = 0; i < numChars; i++) {
        uint32_t off = r32le(data + 16 + (size_t)i * 4);
        // Per-glyph record: [width:u16][reserved:u16, always 0 in practice] then
        // pixel data. Confirmed against every glyph's own table offset gap (zero
        // mismatches across all 260 chars in GFMD) — the reserved field was
        // previously being misread as the glyph's first two pixels, drawing two
        // stray opaque pixels at the top-left corner of every character.
        if (off + 4 > size) continue;
        int w = (int)(data[off] | (data[off + 1] << 8));
        size_t pixBytes = (size_t)w * (size_t)glyphHeight;
        if (off + 4 + pixBytes > size) continue;
        glyphs[i].width = w;
        glyphs[i].pixels.assign(data + off + 4, data + off + 4 + pixBytes);
    }
    return true;
}

int WC3Font::drawText(FrameBuffer* fb, const std::string& text, int x, int y) const {
    if (!fb || glyphs.empty()) return 0;
    int cx = x;
    int fbW = fb->width;
    int fbH = fb->height;
    for (unsigned char ch : text) {
        int idx = (int)ch;
        if (idx < 0 || idx >= numChars) { cx += 4; continue; }
        const Glyph& g = glyphs[idx];
        if (g.width > 0 && !g.pixels.empty()) {
            for (int row = 0; row < glyphHeight; row++) {
                int py = y + row;
                if (py < 0 || py >= fbH) continue;
                for (int col = 0; col < g.width; col++) {
                    uint8_t pix = g.pixels[row * g.width + col];
                    if ((int)pix == transparentIndex) continue;
                    int px = cx + col;
                    if (px < 0 || px >= fbW) continue;
                    fb->framebuffer[py * fbW + px] = pix;
                }
            }
        }
        cx += g.width + 1;
    }
    return cx - x;
}

int WC3Font::drawTextColored(FrameBuffer* fb, const std::string& text,
                             int x, int y, uint8_t colorIdx) const {
    if (!fb || glyphs.empty()) return 0;
    int cx = x;
    int fbW = fb->width, fbH = fb->height;
    for (unsigned char ch : text) {
        int idx = (int)ch;
        if (idx < 0 || idx >= numChars) { cx += 4; continue; }
        const Glyph& g = glyphs[idx];
        if (g.width > 0 && !g.pixels.empty()) {
            for (int row = 0; row < glyphHeight; row++) {
                int py = y + row;
                if (py < 0 || py >= fbH) continue;
                for (int col = 0; col < g.width; col++) {
                    if ((int)g.pixels[row * g.width + col] == transparentIndex) continue;
                    int px = cx + col;
                    if (px < 0 || px >= fbW) continue;
                    fb->framebuffer[py * fbW + px] = colorIdx;
                }
            }
        }
        cx += g.width + 1;
    }
    return cx - x;
}

int WC3Font::drawTextRGBA(uint32_t* rgba, int fbW, int fbH,
                          const std::string& text, int x, int y,
                          uint32_t color) const {
    if (!rgba || glyphs.empty()) return 0;

    auto plotRGBA = [&](int px, int py, uint32_t c) {
        if (px >= 0 && px < fbW && py >= 0 && py < fbH)
            rgba[py * fbW + px] = c;
    };

    int cx = x;
    for (unsigned char ch : text) {
        int idx = (int)ch;
        if (idx < 0 || idx >= numChars) { cx += 4; continue; }
        const Glyph& g = glyphs[idx];
        if (g.width > 0 && !g.pixels.empty()) {
            for (int row = 0; row < glyphHeight; row++) {
                for (int col = 0; col < g.width; col++) {
                    uint8_t pix = g.pixels[row * g.width + col];
                    if ((int)pix == transparentIndex) continue;
                    plotRGBA(cx + col, y + row, color);
                }
            }
        }
        cx += g.width + 1;
    }
    return cx - x;
}

int WC3Font::measureText(const std::string& text) const {
    int cx = 0;
    for (unsigned char ch : text) {
        int idx = (int)ch;
        if (idx >= 0 && idx < numChars && glyphs[idx].width > 0)
            cx += glyphs[idx].width + 1;
        else
            cx += 4;
    }
    return cx;
}

#include "WC4Font.h"
#include <cstring>

uint32_t WC4Font::r32le(const uint8_t* p) {
    return p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24);
}

bool WC4Font::load(const uint8_t* data, size_t size) {
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
        if (off + 2 > size) continue;
        int w = (int)(data[off] | (data[off + 1] << 8));
        size_t pixBytes = (size_t)w * (size_t)glyphHeight;
        if (off + 2 + pixBytes > size) continue;
        glyphs[i].width = w;
        glyphs[i].pixels.assign(data + off + 2, data + off + 2 + pixBytes);
    }
    return true;
}

int WC4Font::drawText(FrameBuffer* fb, const std::string& text, int x, int y) const {
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

int WC4Font::drawTextRGBA(uint32_t* rgba, int fbW, int fbH,
                          const std::string& text, int x, int y,
                          uint32_t color) const {
    if (!rgba || glyphs.empty()) return 0;
    constexpr uint32_t kShadow = 0xFF000000u; // black, full alpha

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
                    plotRGBA(cx + col + 1, y + row + 1, kShadow);
                    plotRGBA(cx + col,     y + row,     color);
                }
            }
        }
        cx += g.width + 1;
    }
    return cx - x;
}

int WC4Font::measureText(const std::string& text) const {
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

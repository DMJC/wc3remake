#include "RSWC3Shape.h"
#include "RSImageSet.h"
#include "RLEShape.h"
#include <vector>

namespace {

uint32_t ReadU32LE(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint16_t ReadU16LE(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

// Row-based RLE stream used by every frame: 0x00 advances to the next row,
// 0x01 skips (leaves transparent) the next `n` columns, and any other byte's
// low bit selects a literal texel run (odd) or a single-color run-length
// fill (even), both of length (byte >> 1).
//
// col/row (both always starting at 0, per row/stream respectively) are
// already final destination coordinates, confirmed by tracing the raw
// opcode stream of GLOBALS.IFF's PNTR>SHAP cursor set: frame 0 (the arrow,
// whose header x_blit/y_min are 0) touches col/row 0..12/0..15, matching
// its header's x_scan_max/y_max exactly and rendering correctly, while
// frames with nonzero (and, per their raw bytes, negative-as-int16) header
// x_blit/y_min — e.g. frame 6's "head" cursor — touch col/row spanning
// nearly the *entire* 30x31 canvas on their own (0..26/0..29), regardless
// of their own x_scan_max/y_max. An earlier version of this function added
// header x_blit/y_min onto col/row before writing each pixel (correcting a
// prior unsigned/signed misread of those fields that made every non-arrow
// frame decode fully transparent — see git history) — but per the above,
// that addition was itself wrong: it shifted already-correct destination
// coordinates by another ~13-15px up/left, clipping most of the icon
// off-canvas and leaving only its bottom-right corner. Header x_blit/y_min
// are still parsed (SubInfo, below) since they plausibly encode a cursor
// hotspot for on-screen positioning, but they play no part in decoding.
void DecodeRle(const uint8_t* src, size_t srcLen, size_t ySpan, size_t /*rowWidth*/,
                uint8_t* frame, size_t fw, size_t fh) {
    size_t row = 0, col = 0, i = 0;
    while (i < srcLen && row < ySpan) {
        uint8_t key = src[i++];
        if (key == 0) {
            row++;
            col = 0;
            continue;
        }
        if (key == 1) {
            if (i < srcLen) col += src[i++];
            continue;
        }
        size_t count = (size_t)(key >> 1);
        bool lit = (key & 1) != 0;
        if (count == 0) continue;
        if (lit) {
            for (size_t k = 0; k < count && i < srcLen && row < ySpan; k++, col++) {
                uint8_t color = src[i++];
                if (row < fh && col < fw) frame[row * fw + col] = color;
            }
        } else {
            if (i >= srcLen) break;
            uint8_t color = src[i++];
            for (size_t k = 0; k < count && row < ySpan; k++, col++) {
                if (row < fh && col < fw) frame[row * fw + col] = color;
            }
        }
    }
}

}  // namespace

RSImageSet* RSWC3DecodeShapeEntry(const uint8_t* data, size_t size) {
    RSImageSet* img = new RSImageSet();
    // Version digit varies by asset family (gump sprites and cockpit art use
    // "1.11"; WRLD's STAR-glint sprites use "1.10") but the byte layout is
    // otherwise identical, so only the "1.1" prefix is checked here.
    if (size < 8 || data[0] != '1' || data[1] != '.' || data[2] != '1') {
        return img;
    }

    uint32_t N = ReadU32LE(data + 4);
    if (N == 0 || N > 1024 || 8 + (size_t)N * 8 > size) {
        return img;
    }

    struct SubInfo {
        uint16_t full_w, full_h;
        int32_t x_blit, y_min;
        uint16_t row_width;
        int32_t y_span;
        size_t pdata_off, pdata_len;
    };
    std::vector<SubInfo> subs;
    subs.reserve(N);

    for (uint32_t i = 0; i < N; i++) {
        uint32_t si_off = ReadU32LE(data + 8 + i * 8);
        if ((size_t)si_off + 24 > size) continue;

        const uint8_t* h = data + si_off;
        uint16_t full_h = (uint16_t)(ReadU16LE(h + 0) + 1);
        uint16_t full_w = (uint16_t)(ReadU16LE(h + 2) + 1);
        // Signed (encoded as int16_t) — confirmed against GLOBALS.IFF's
        // PNTR>SHAP cursor set, whose non-arrow frames have raw values like
        // 0xFFF3/0xFFF1 (-13/-15). Not used in decoding (see DecodeRle's
        // comment) — plausibly a cursor hotspot for on-screen positioning,
        // not yet wired up anywhere — but y_min still feeds y_span below,
        // and needs the correct sign to do that right.
        int32_t x_blit = (int16_t)ReadU16LE(h + 8);
        int32_t y_min = (int16_t)ReadU16LE(h + 12);
        uint16_t x_scan_max = ReadU16LE(h + 16);
        int32_t y_max = (int16_t)ReadU16LE(h + 20);

        if (y_min > y_max) y_max = y_min;
        if (x_scan_max >= full_w) x_scan_max = (uint16_t)(full_w - 1);
        if (y_max >= (int32_t)full_h) y_max = (int32_t)full_h - 1;

        uint16_t row_width = (uint16_t)(x_scan_max + 1);
        // y_max - y_min spans the full real row extent of this sub-image's
        // content (confirmed by tracing the raw opcode stream — e.g. a
        // -15..14 range decodes to real pixel writes touching row 0..29),
        // unlike x, which has no analogous span field (DecodeRle's rowWidth
        // param is unused; the per-pixel `col < fw` bounds check is the only
        // horizontal limit needed).
        int32_t y_span = y_max - y_min + 1;

        size_t pdata_off = (size_t)si_off + 24;
        size_t pdata_len;
        if (i + 1 < N) {
            uint32_t next_off = ReadU32LE(data + 8 + (i + 1) * 8);
            pdata_len = (next_off > pdata_off) ? next_off - pdata_off : 0;
        } else {
            pdata_len = (size > pdata_off) ? size - pdata_off : 0;
        }

        subs.push_back({full_w, full_h, x_blit, y_min, row_width, y_span, pdata_off, pdata_len});
    }

    if (subs.empty()) {
        return img;
    }

    uint16_t full_w = subs[0].full_w;
    uint16_t full_h = subs[0].full_h;
    if ((size_t)full_w * full_h == 0 || (size_t)full_w * full_h > 4 * 1024 * 1024) {
        return img;
    }

    // Sub1's data being much smaller than sub0's indicates delta animation
    // mode: later frames only encode the pixels that changed, patched onto
    // a running copy of the previous frame rather than redrawn from scratch.
    bool delta_mode = subs.size() > 1 && subs[1].pdata_len * 2 < subs[0].pdata_len;

    std::vector<uint8_t> running_frame((size_t)full_w * full_h, 0xFF);
    {
        const SubInfo& s = subs[0];
        DecodeRle(data + s.pdata_off, s.pdata_len, s.y_span, s.row_width,
                  running_frame.data(), full_w, full_h);
    }
    {
        RLEShape* shape = new RLEShape();
        shape->InitFromPixels(running_frame.data(), full_w, full_h);
        // x_blit/y_min from the per-frame header encode the draw position for
        // cockpit/sprite shapes. Callers that need a different position (e.g.
        // SCMouse::draw for cursor shapes) always call SetPosition before
        // drawing, so pre-seeding here is safe across all call sites.
        shape->position = {subs[0].x_blit, subs[0].y_min};
        img->Add(shape);
    }

    for (size_t i = 1; i < subs.size(); i++) {
        const SubInfo& s = subs[i];
        // Sub-images with their own, different full_w/full_h aren't
        // animation frames of a shared canvas at all — they're independent,
        // differently-sized images (e.g. WRLD>STAR's 7x7/5x5/4x4 glint
        // sprites, all sharing one "1.1x" entry). Decode those at their own
        // size instead of forcing them through the delta/keyframe canvas.
        if (s.full_w != full_w || s.full_h != full_h) {
            std::vector<uint8_t> ownFrame((size_t)s.full_w * s.full_h, 0xFF);
            DecodeRle(data + s.pdata_off, s.pdata_len, s.y_span, s.row_width,
                      ownFrame.data(), s.full_w, s.full_h);
            RLEShape* shape = new RLEShape();
            shape->InitFromPixels(ownFrame.data(), s.full_w, s.full_h);
            shape->position = {s.x_blit, s.y_min};
            img->Add(shape);
            continue;
        }
        if (!delta_mode) {
            running_frame.assign((size_t)full_w * full_h, 0xFF);
        }
        DecodeRle(data + s.pdata_off, s.pdata_len, s.y_span, s.row_width,
                  running_frame.data(), full_w, full_h);
        RLEShape* shape = new RLEShape();
        shape->InitFromPixels(running_frame.data(), full_w, full_h);
        shape->position = {s.x_blit, s.y_min};
        img->Add(shape);
    }

    return img;
}

std::unordered_map<uint32_t, RSImageSet*> RSWC3DecodeShapePak(const uint8_t* data, size_t size) {
    std::unordered_map<uint32_t, RSImageSet*> result;
    size_t pos = 0;
    while (pos + 8 <= size) {
        uint32_t id = ReadU32LE(data + pos);
        uint32_t bodySize = ReadU32LE(data + pos + 4);
        size_t bodyStart = pos + 8;
        if (bodyStart + (size_t)bodySize > size) break;
        if (bodySize < 4 || data[bodyStart] != '1' || data[bodyStart + 1] != '.' ||
            data[bodyStart + 2] != '1' || data[bodyStart + 3] != '1') {
            break;
        }
        result[id] = RSWC3DecodeShapeEntry(data + bodyStart, bodySize);
        pos = bodyStart + bodySize;
    }
    return result;
}

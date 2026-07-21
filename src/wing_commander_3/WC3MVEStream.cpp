#include "WC3MVEStream.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

#include <cstring>
#include <cstdio>

// Same IFF chunk helpers as WC3MVEPlayer.cpp (big-endian size field) —
// kept as a separate local copy rather than shared, matching this
// codebase's existing pattern of not sharing tiny static helpers across
// translation units.
static inline int mve_chunk_size(const uint8_t* p) {
    return (p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7];
}
static inline int mve_chunk_total(const uint8_t* p) {
    int sz = mve_chunk_size(p);
    return 8 + sz + (sz & 1);
}

WC3MVEStream::WC3MVEStream() {}

WC3MVEStream::~WC3MVEStream() {
    if (vfrm) av_frame_free(&vfrm);
    if (vpkt) av_packet_free(&vpkt);
    if (vctx) avcodec_free_context(&vctx);
}

bool WC3MVEStream::load(const uint8_t* raw, size_t sz) {
    if (!raw || sz < 12 || memcmp(raw, "FORM", 4) || memcmp(raw + 8, "MOVE", 4)) {
        printf("WC3MVEStream: not a FORM MOVE file\n");
        return false;
    }

    int form_end = 12 + ((raw[4] << 24) | (raw[5] << 16) | (raw[6] << 8) | raw[7]);
    if (form_end > (int)sz) form_end = (int)sz;

    bool is_first_frame = true;
    bool at_shot_start = true;
    const uint8_t* cur_shot = nullptr;
    int cur_shot_sz = 0;

    FramePair pending{};
    for (int p = 12; p + 8 <= form_end;) {
        int csz = mve_chunk_size(raw + p);
        if (csz < 0 || p + 8 + csz > form_end) break;

        if (!memcmp(raw + p, "PALT", 4)) {
            palettes.push_back(raw + p);
            palette_totals.push_back(mve_chunk_total(raw + p));
        } else if (!memcmp(raw + p, "SHOT", 4) && csz >= 4) {
            cur_shot = raw + p;
            cur_shot_sz = mve_chunk_total(raw + p);
            at_shot_start = true;
        } else if (!memcmp(raw + p, "VGA ", 4)) {
            if (pending.vga) frames.push_back(pending);
            pending = {};
            pending.vga = raw + p;
            pending.vga_total = mve_chunk_total(raw + p);
            pending.shot_chunk = at_shot_start ? cur_shot : nullptr;
            pending.shot_total = at_shot_start ? cur_shot_sz : 0;
            pending.first_ever = is_first_frame;
            is_first_frame = false;
            at_shot_start = false;
        }
        // AUDI chunks deliberately not collected — see this class's own
        // header comment for why.
        p += 8 + csz + (csz & 1);
    }
    if (pending.vga) frames.push_back(pending);

    if (palettes.empty() || frames.empty()) {
        printf("WC3MVEStream: missing PALT or no VGA frames\n");
        return false;
    }

    const AVCodec* vcodec = avcodec_find_decoder(AV_CODEC_ID_XAN_WC3);
    if (!vcodec) {
        printf("WC3MVEStream: xan_wc3 codec not found\n");
        return false;
    }
    vctx = avcodec_alloc_context3(vcodec);
    vctx->width = 320;
    vctx->height = 200;
    if (avcodec_open2(vctx, vcodec, nullptr) < 0) {
        printf("WC3MVEStream: failed to open xan_wc3 decoder\n");
        avcodec_free_context(&vctx);
        return false;
    }
    vpkt = av_packet_alloc();
    vfrm = av_frame_alloc();
    loaded = true;
    return true;
}

bool WC3MVEStream::decodeNextFrame() {
    if (nextFrameIndex >= frames.size()) {
        finished = true;
        return false;
    }
    const FramePair& fr = frames[nextFrameIndex];
    nextFrameIndex++;

    int all_pal_sz = 0;
    if (fr.first_ever) {
        for (int t : palette_totals) all_pal_sz += t;
    }
    int total = (fr.first_ever ? all_pal_sz : 0) + fr.shot_total + fr.vga_total;

    uint8_t* buf = (uint8_t*)av_malloc(total + AV_INPUT_BUFFER_PADDING_SIZE);
    int off = 0;
    if (fr.first_ever) {
        for (size_t i = 0; i < palettes.size(); i++) {
            memcpy(buf + off, palettes[i], palette_totals[i]);
            off += palette_totals[i];
        }
    }
    if (fr.shot_chunk) {
        memcpy(buf + off, fr.shot_chunk, fr.shot_total);
        off += fr.shot_total;
    }
    memcpy(buf + off, fr.vga, fr.vga_total);
    memset(buf + total, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    av_packet_from_data(vpkt, buf, total);

    av_frame_unref(vfrm);
    bool decoded = false;
    if (avcodec_send_packet(vctx, vpkt) == 0 && avcodec_receive_frame(vctx, vfrm) == 0) {
        // xan_wc3 delivers AV_PIX_FMT_PAL8: data[0]=palette indices
        // (stride linesize[0]), data[1]=256x uint32 palette (0xAARRGGBB
        // native) — same conversion WC3MVEPlayer::play() uses, flipping Y
        // since GL's own origin is bottom-left (kept here even though
        // this class doesn't draw with GL itself, so callers get pixels
        // in the same top-to-bottom row order everything else in this
        // codebase's FrameBuffer/RLEShape pipeline expects).
        uint32_t* pal = (uint32_t*)vfrm->data[1];
        uint8_t* idx = vfrm->data[0];
        int stride = vfrm->linesize[0];
        for (int y = 0; y < 200; y++) {
            const uint8_t* row = idx + (199 - y) * stride;
            uint32_t* dst = pixels + y * 320;
            for (int x = 0; x < 320; x++) {
                uint32_t c = pal[row[x]];
                dst[x] = ((c >> 16) & 0xFF) | (c & 0x0000FF00u) | ((c & 0xFF) << 16) | 0xFF000000u;
            }
        }
        decoded = true;
    }
    av_packet_unref(vpkt);
    return decoded;
}

void WC3MVEStream::advance(float dt) {
    if (!loaded || finished) {
        return;
    }
    // Same 66ms/frame (~15fps) real pacing as WC3MVEPlayer's own
    // SDL_Delay(66-elapsed) loop.
    const float kFrameSeconds = 0.066f;
    accumulator += dt;
    while (accumulator >= kFrameSeconds) {
        accumulator -= kFrameSeconds;
        if (!decodeNextFrame()) {
            break;
        }
    }
}

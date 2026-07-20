#include "WC4MVEPlayer.h"
#include "../engine/RSScreen.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/channel_layout.h>
}

#include <SDL2/SDL.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <cstring>
#include <cstdio>
#include <vector>

// ---------------------------------------------------------------------------
// IFF chunk helpers (big-endian size field)
// ---------------------------------------------------------------------------

static inline int chunk_size(const uint8_t* p) {
    return (p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7];
}

static inline int chunk_total(const uint8_t* p) {
    int sz = chunk_size(p);
    return 8 + sz + (sz & 1);  // header + data + optional pad byte
}

// ---------------------------------------------------------------------------
// Packet building
//
// The xan_wc4 codec maintains an internal palette TABLE (up to 256 slots).
// Each PALT chunk received adds one slot; SHOT(N) selects slot N as current.
// Sending PALT with every frame would overflow the 256-slot limit (the codec
// returns AVERROR_INVALIDDATA once num_pals > 255, aborting before VGA decode).
//
// Correct feed pattern:
//   frame 0 : [all PALT chunks] + [SHOT(0)] + [VGA]   <- builds the whole table
//   frames 1..N-1 (same shot): [VGA]                  <- uses current palette
//   first frame of shot K: [SHOT(K)] + [VGA]          <- switches palette slot
// ---------------------------------------------------------------------------

void WC4MVEPlayer::play(const uint8_t* raw, size_t sz) {
    if (sz < 12 || memcmp(raw, "FORM", 4) || memcmp(raw + 8, "MOVE", 4)) {
        printf("WC4MVE: not a FORM MOVE file\n");
        return;
    }

    // --- Parse IFF container ---
    std::vector<const uint8_t*> palettes;
    std::vector<int>            palette_totals;

    struct FramePair {
        const uint8_t* vga;
        int vga_total;
        const uint8_t* audi;      // raw S16LE PCM
        int audi_sz;
        const uint8_t* shot_chunk; // SHOT IFF chunk preceding this VGA (may be null)
        int shot_total;
        bool first_ever;           // true only for the absolute first VGA frame
    };
    std::vector<FramePair> frames;

    int form_end = 12 + ((raw[4] << 24) | (raw[5] << 16) | (raw[6] << 8) | raw[7]);
    if (form_end > (int)sz) form_end = (int)sz;

    bool is_first_frame        = true;
    bool at_shot_start         = true;   // first VGA after a SHOT
    const uint8_t* cur_shot    = nullptr;
    int            cur_shot_sz = 0;

    FramePair pending{};
    for (int p = 12; p + 8 <= form_end;) {
        int csz = chunk_size(raw + p);
        if (csz < 0 || p + 8 + csz > form_end) break;

        if (!memcmp(raw + p, "PALT", 4)) {
            palettes.push_back(raw + p);
            palette_totals.push_back(chunk_total(raw + p));
        } else if (!memcmp(raw + p, "SHOT", 4) && csz >= 4) {
            cur_shot    = raw + p;
            cur_shot_sz = chunk_total(raw + p);
            at_shot_start = true;
        } else if (!memcmp(raw + p, "VGA ", 4)) {
            if (pending.vga) frames.push_back(pending);
            pending = {};
            pending.vga        = raw + p;
            pending.vga_total  = chunk_total(raw + p);
            pending.shot_chunk = at_shot_start ? cur_shot    : nullptr;
            pending.shot_total = at_shot_start ? cur_shot_sz : 0;
            pending.first_ever = is_first_frame;
            is_first_frame     = false;
            at_shot_start      = false;
        } else if (!memcmp(raw + p, "AUDI", 4)) {
            pending.audi    = raw + p + 8;
            pending.audi_sz = csz;
        }
        p += 8 + csz + (csz & 1);
    }
    if (pending.vga) frames.push_back(pending);

    if (palettes.empty() || frames.empty()) {
        printf("WC4MVE: missing PALT or no VGA frames\n");
        return;
    }

    printf("WC4MVE: %zu frames, %zu palettes\n", frames.size(), palettes.size());

    // --- xan_wc4 video decoder ---
    const AVCodec* vcodec = avcodec_find_decoder(AV_CODEC_ID_XAN_WC4);
    if (!vcodec) { printf("WC4MVE: xan_wc4 codec not found\n"); return; }
    AVCodecContext* vctx = avcodec_alloc_context3(vcodec);
    vctx->width  = 320;
    vctx->height = 200;
    if (avcodec_open2(vctx, vcodec, nullptr) < 0) {
        printf("WC4MVE: failed to open xan_wc4 decoder\n");
        avcodec_free_context(&vctx);
        return;
    }

    // --- SDL audio device: AUDI chunks are raw S16LE PCM at 22050 Hz mono ---
    SDL_AudioDeviceID audiodev = 0;
    {
        SDL_AudioSpec want{}, got{};
        want.freq     = 22050;
        want.format   = AUDIO_S16LSB;
        want.channels = 1;
        want.samples  = 512;
        audiodev = SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);
        if (audiodev) SDL_PauseAudioDevice(audiodev, 0);
    }

    AVPacket* vpkt = av_packet_alloc();
    AVFrame*  vfrm = av_frame_alloc();

    RSScreen& screen = RSScreen::instance();

    // Each AUDI chunk is 2940 bytes = 1470 S16 samples = 66.67 ms at 22050 Hz.
    // Keep at most 2 frames of audio buffered ahead.
    const uint32_t k_max_queued = 2940u * 2u;

    // Pre-compute total size of all palette chunks for the first-frame mega-packet.
    int all_pal_sz = 0;
    for (int i = 0; i < (int)palettes.size(); i++) all_pal_sz += palette_totals[i];

    bool done = false;
    for (auto& fr : frames) {
        if (done) break;

        uint32_t frame_start = SDL_GetTicks();

        // --- Build packet ---
        // first_ever : all PALTs + optional SHOT + VGA
        // shot start : SHOT + VGA
        // normal     : VGA only
        int total;
        if (fr.first_ever) {
            total = all_pal_sz + fr.shot_total + fr.vga_total;
        } else {
            total = fr.shot_total + fr.vga_total;
        }

        uint8_t* buf = (uint8_t*)av_malloc(total + AV_INPUT_BUFFER_PADDING_SIZE);
        int off = 0;
        if (fr.first_ever) {
            for (int i = 0; i < (int)palettes.size(); i++) {
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

        // --- Decode and render ---
        av_frame_unref(vfrm);
        if (avcodec_send_packet(vctx, vpkt) == 0 &&
            avcodec_receive_frame(vctx, vfrm) == 0) {

            // xan_wc4 delivers AV_PIX_FMT_PAL8:
            //   data[0] = palette indices, stride = linesize[0]
            //   data[1] = 256 × uint32_t palette entries (0xAARRGGBB native)
            uint32_t* pal    = (uint32_t*)vfrm->data[1];
            uint8_t*  idx    = vfrm->data[0];
            int       stride = vfrm->linesize[0];

            // Convert to GL_RGBA while flipping Y (GL origin is bottom-left)
            uint32_t pixels[320 * 200];
            for (int y = 0; y < 200; y++) {
                const uint8_t* row = idx + (199 - y) * stride;
                uint32_t*      dst = pixels + y * 320;
                for (int x = 0; x < 320; x++) {
                    uint32_t c = pal[row[x]];  // 0xAARRGGBB
                    dst[x] = ((c >> 16) & 0xFF)    // R
                           | (c & 0x0000FF00u)      // G
                           | ((c & 0xFF) << 16)     // B
                           | 0xFF000000u;           // A=255
                }
            }

            glViewport(0, 0, screen.width, screen.height);
            glMatrixMode(GL_PROJECTION); glLoadIdentity();
            glOrtho(0, screen.width, 0, screen.height, -1, 1);
            glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
            glDisable(GL_TEXTURE_2D);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_LIGHTING);
            glDisable(GL_FOG);
            glDisable(GL_BLEND);

            // Scale uniformly to match window width, centered vertically.
            // glRasterPos2i(0,0) is always in-bounds; glBitmap then shifts the
            // current raster position without re-running GL's clip test on it
            // (a negative glRasterPos2i would be discarded as invalid).
            float zoom     = (float)screen.width / 320.0f;
            float y_offset = (screen.height - 200.0f * zoom) / 2.0f;
            glRasterPos2i(0, 0);
            glBitmap(0, 0, 0, 0, 0.0f, y_offset, nullptr);
            glPixelZoom(zoom, zoom);
            glDrawPixels(320, 200, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        }
        av_packet_unref(vpkt);

        // --- Audio: queue raw S16LE PCM directly ---
        if (audiodev && fr.audi && fr.audi_sz > 0) {
            while (!done && SDL_GetQueuedAudioSize(audiodev) > k_max_queued) {
                SDL_Event ev;
                while (SDL_PollEvent(&ev)) {
                    if (ev.type == SDL_QUIT) { done = true; break; }
                    if (ev.type == SDL_KEYDOWN &&
                        ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE) { done = true; break; }
                }
                SDL_Delay(5);
            }
            SDL_QueueAudio(audiodev, fr.audi, (uint32_t)fr.audi_sz);
        }

        // --- Present ---
        screen.refresh();

        // --- Poll events ---
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { done = true; break; }
            if (ev.type == SDL_KEYDOWN &&
                ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE) { done = true; break; }
        }

        // --- Frame timing: pace to audio (66.67 ms per frame at 15 fps) ---
        uint32_t elapsed = SDL_GetTicks() - frame_start;
        if (elapsed < 66u) SDL_Delay(66u - elapsed);
    }

    // Drain remaining audio before returning
    if (audiodev) {
        while (!done && SDL_GetQueuedAudioSize(audiodev) > 0) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT || (ev.type == SDL_KEYDOWN &&
                    ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE)) { done = true; break; }
            }
            SDL_Delay(10);
        }
        SDL_CloseAudioDevice(audiodev);
    }

    av_frame_free(&vfrm);
    av_packet_free(&vpkt);
    avcodec_free_context(&vctx);
}

void WC4MVEPlayer::playShot(const uint8_t* raw, size_t sz, int shotIndex) {
    if (!raw || sz < 12 || memcmp(raw, "FORM", 4) || memcmp(raw + 8, "MOVE", 4)) return;
    if (shotIndex < 0) return;

    // Parse like play() but tag each VGA frame with its shot number.
    std::vector<const uint8_t*> palettes;
    std::vector<int>            palette_totals;

    struct FramePair {
        const uint8_t* vga; int vga_total;
        const uint8_t* audi; int audi_sz;
        const uint8_t* shot_chunk; int shot_total;
        bool first_ever;
        int shot_num;
    };
    std::vector<FramePair> frames;

    int form_end = 12 + ((raw[4]<<24)|(raw[5]<<16)|(raw[6]<<8)|raw[7]);
    if (form_end > (int)sz) form_end = (int)sz;

    bool is_first = true;
    int cur_shot_num = -1;
    const uint8_t* cur_shot = nullptr; int cur_shot_sz = 0;

    FramePair pending{};
    for (int p = 12; p + 8 <= form_end;) {
        int csz = chunk_size(raw + p);
        if (csz < 0 || p + 8 + csz > form_end) break;
        if (!memcmp(raw+p,"PALT",4)) {
            palettes.push_back(raw+p); palette_totals.push_back(chunk_total(raw+p));
        } else if (!memcmp(raw+p,"SHOT",4) && csz>=4) {
            cur_shot_num++;
            cur_shot = raw+p; cur_shot_sz = chunk_total(raw+p);
        } else if (!memcmp(raw+p,"VGA ",4)) {
            if (pending.vga) frames.push_back(pending);
            pending = {};
            pending.vga = raw+p; pending.vga_total = chunk_total(raw+p);
            pending.shot_chunk = cur_shot; pending.shot_total = cur_shot_sz;
            cur_shot = nullptr;  // only attach to first VGA in this shot
            pending.first_ever = is_first; is_first = false;
            pending.shot_num = cur_shot_num;
        } else if (!memcmp(raw+p,"AUDI",4)) {
            pending.audi = raw+p+8; pending.audi_sz = csz;
        }
        p += 8 + csz + (csz & 1);
    }
    if (pending.vga) frames.push_back(pending);

    if (palettes.empty() || frames.empty()) return;

    // Collect only frames belonging to the requested shot
    std::vector<FramePair> shot_frames;
    bool seen_shot = false;
    for (auto& fr : frames) {
        if (fr.shot_num == shotIndex) { seen_shot = true; shot_frames.push_back(fr); }
        else if (seen_shot) break;  // past the requested shot
    }
    if (shot_frames.empty()) {
        printf("WC4MVE: shot %d not found\n", shotIndex);
        return;
    }

    // Mark first frame of this sub-sequence as first_ever so the codec gets all palettes
    shot_frames[0].first_ever = true;

    printf("WC4MVE: playing shot %d (%zu frames)\n", shotIndex, shot_frames.size());

    const AVCodec* vcodec = avcodec_find_decoder(AV_CODEC_ID_XAN_WC4);
    if (!vcodec) return;
    AVCodecContext* vctx = avcodec_alloc_context3(vcodec);
    vctx->width = 320; vctx->height = 200;
    if (avcodec_open2(vctx, vcodec, nullptr) < 0) { avcodec_free_context(&vctx); return; }

    SDL_AudioDeviceID audiodev = 0;
    { SDL_AudioSpec want{}, got{};
      want.freq=22050; want.format=AUDIO_S16LSB; want.channels=1; want.samples=512;
      audiodev = SDL_OpenAudioDevice(nullptr,0,&want,&got,0);
      if (audiodev) SDL_PauseAudioDevice(audiodev,0); }

    AVPacket* vpkt = av_packet_alloc();
    AVFrame*  vfrm = av_frame_alloc();
    RSScreen& screen = RSScreen::instance();
    const uint32_t k_max_queued = 2940u * 2u;
    int all_pal_sz = 0;
    for (int i=0; i<(int)palettes.size(); i++) all_pal_sz += palette_totals[i];

    bool done = false;
    for (auto& fr : shot_frames) {
        if (done) break;
        uint32_t frame_start = SDL_GetTicks();

        int total = fr.first_ever ? all_pal_sz + fr.shot_total + fr.vga_total
                                  : fr.shot_total + fr.vga_total;
        uint8_t* buf = (uint8_t*)av_malloc(total + AV_INPUT_BUFFER_PADDING_SIZE);
        int off = 0;
        if (fr.first_ever)
            for (int i=0; i<(int)palettes.size(); i++) {
                memcpy(buf+off, palettes[i], palette_totals[i]); off += palette_totals[i]; }
        if (fr.shot_chunk) { memcpy(buf+off, fr.shot_chunk, fr.shot_total); off += fr.shot_total; }
        memcpy(buf+off, fr.vga, fr.vga_total);
        memset(buf+total, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        av_packet_from_data(vpkt, buf, total);

        av_frame_unref(vfrm);
        if (avcodec_send_packet(vctx, vpkt)==0 && avcodec_receive_frame(vctx, vfrm)==0) {
            uint32_t* pal=((uint32_t*)vfrm->data[1]); uint8_t* idx=vfrm->data[0]; int stride=vfrm->linesize[0];
            uint32_t pixels[320*200];
            for (int y=0; y<200; y++) {
                const uint8_t* row=idx+(199-y)*stride; uint32_t* dst=pixels+y*320;
                for (int x=0; x<320; x++) {
                    uint32_t c=pal[row[x]];
                    dst[x]=((c>>16)&0xFF)|(c&0xFF00)|((c&0xFF)<<16)|0xFF000000u;
                }
            }
            glViewport(0,0,screen.width,screen.height);
            glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0,screen.width,0,screen.height,-1,1);
            glMatrixMode(GL_MODELVIEW); glLoadIdentity();
            glDisable(GL_TEXTURE_2D); glDisable(GL_DEPTH_TEST); glDisable(GL_LIGHTING);
            glDisable(GL_FOG); glDisable(GL_BLEND);
            float zoom = (float)screen.width/320.f;
            float y_offset = (screen.height - 200.f*zoom) / 2.f;
            glRasterPos2i(0,0);
            glBitmap(0,0,0,0,0.0f,y_offset,nullptr);
            glPixelZoom(zoom, zoom);
            glDrawPixels(320,200,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        }
        av_packet_unref(vpkt);

        if (audiodev && fr.audi && fr.audi_sz>0) {
            while (!done && SDL_GetQueuedAudioSize(audiodev)>k_max_queued) {
                SDL_Event ev;
                while (SDL_PollEvent(&ev)) {
                    if (ev.type==SDL_QUIT||(ev.type==SDL_KEYDOWN&&ev.key.keysym.scancode==SDL_SCANCODE_ESCAPE)) done=true;
                }
                SDL_Delay(5);
            }
            SDL_QueueAudio(audiodev, fr.audi, (uint32_t)fr.audi_sz);
        }

        screen.refresh();

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type==SDL_QUIT||(ev.type==SDL_KEYDOWN&&ev.key.keysym.scancode==SDL_SCANCODE_ESCAPE)) done=true;
        }

        uint32_t elapsed = SDL_GetTicks()-frame_start;
        if (elapsed < 66u) SDL_Delay(66u-elapsed);
    }

    if (audiodev) {
        while (!done && SDL_GetQueuedAudioSize(audiodev)>0) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type==SDL_QUIT||(ev.type==SDL_KEYDOWN&&ev.key.keysym.scancode==SDL_SCANCODE_ESCAPE)) done=true;
            }
            SDL_Delay(10);
        }
        SDL_CloseAudioDevice(audiodev);
    }
    av_frame_free(&vfrm); av_packet_free(&vpkt); avcodec_free_context(&vctx);
}

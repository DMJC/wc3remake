#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

struct AVCodecContext;
struct AVPacket;
struct AVFrame;

// Non-blocking, per-tick MVE decoder for embedding small video playback
// inside an otherwise-live scene (2026-07 session: the cockpit's left VDU
// playing a comm-reply "radio face" clip while the rest of the cockpit/
// flight sim keeps rendering normally). Unlike WC3MVEPlayer::play()/
// playShot()/playBranchGroup() — which each own a blocking SDL_Delay loop,
// their own SDL audio device, and their own event poll, and draw straight
// to the full screen viewport via raw GL calls — this class only parses
// the container and decodes frames; the caller advances it once per
// render tick and reads back whatever the current frame is, drawing it
// wherever (and however large) it wants.
//
// Deliberately doesn't touch AUDI chunks at all: RADI_FMV's own doc
// comment describes this as a "radio face" video clip shown *alongside* a
// wingman's already-separately-voiced spoken line (RSProf::radi.sond/
// spch, already played by SCMissionActors::setMessage) — not a
// replacement audio track — so there's no clear second audio source that
// should also be playing, and this avoids any risk of double/desynced
// audio. If a real case turns up where an .mve's own AUDI carries
// meaningful audio, that's a separate follow-up.
class WC3MVEStream {
public:
    WC3MVEStream();
    ~WC3MVEStream();

    // Parses the FORM MOVE container and opens the xan_wc3 codec. Returns
    // false (and leaves the stream unusable) if the data isn't a valid
    // MVE or the codec can't be opened. Does NOT take ownership of `data`
    // — the caller must keep the buffer alive for the lifetime of this
    // stream (matches TreEntry's own caller-owns-the-buffer convention
    // used everywhere else in this codebase).
    bool load(const uint8_t* data, size_t size);

    // Advances playback by dt seconds; decodes the next frame once enough
    // time has accumulated (66ms/frame, the same 15fps pacing
    // WC3MVEPlayer's own SDL_Delay loop uses). Once every frame has been
    // shown, isFinished() latches true and further advance() calls are a
    // no-op.
    void advance(float dt);
    bool isFinished() const { return finished; }
    bool isLoaded() const { return loaded; }

    // 320x200 RGBA8888, byte order 0xAABBGGRR in memory (the exact
    // GL_RGBA/GL_UNSIGNED_BYTE layout WC3MVEPlayer's own glDrawPixels
    // call already produces) — valid once at least one frame has decoded
    // (i.e. after the first advance() call that doesn't immediately
    // return with frameCount()==0).
    const uint32_t* getPixels() const { return pixels; }

private:
    struct FramePair {
        const uint8_t* vga{nullptr};
        int vga_total{0};
        const uint8_t* shot_chunk{nullptr};
        int shot_total{0};
        bool first_ever{false};
    };
    std::vector<FramePair> frames;
    std::vector<const uint8_t*> palettes;
    std::vector<int> palette_totals;
    size_t nextFrameIndex{0};
    float accumulator{0.0f};
    bool finished{false};
    bool loaded{false};

    AVCodecContext* vctx{nullptr};
    AVPacket* vpkt{nullptr};
    AVFrame* vfrm{nullptr};

    uint32_t pixels[320 * 200]{};

    bool decodeNextFrame();
};

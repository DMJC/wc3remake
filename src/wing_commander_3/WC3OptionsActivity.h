#pragma once
#include <functional>
#include <vector>
#include <string>
#include "../engine/IActivity.h"
#include "../engine/FrameBuffer.h"
#include "../realspace/RSImageSet.h"
#include "../realspace/RSPalette.h"
#include "WC3Options.h"

// Standalone Options overlay — pushed on top of whatever activity is
// currently running (the terminal's Hub "controls" tab, or a global Alt+O
// during flight), so there is exactly one Options implementation reachable
// both ways rather than two parallel UIs.
//
// Rebuilt to use the real WC3 options-screen assets (DATA\SCREENS\ESFOPTS*,
// found inside MISSIONS.TRE) instead of a hand-drawn solid-color panel:
//   ESFOPTSS.VFX — 640x480 background (1 frame, "1.11" WC3 shape format —
//     see RSWC3Shape.h, the same decoder already used for cockpit
//     instrument art).
//   ESFOPTSI.VFX — 5 frames, same format: frame 0 is a 16x14 checkbox/
//     marker icon, frames 1-4 are 4 wider (90x19/83x19/58x19/112x19)
//     button-label sprites that line up with the layout table's 4 bottom
//     action-button records.
//   ESFOPTSD.DAT — a real layout table (not reverse-engineered from
//     scratch — decoded and cross-checked against the .VFX frame counts):
//     a leading uint32 record count (36), then that many 4-uint32 records
//     of [buttonIndex, groupId, x, y]. Reads as 3 columns of checkbox rows
//     at x=89/240/431 (15px apart vertically), each column's rows grouped
//     into radio-button-style groups (groupId, 0xFFFFFFFF = ungrouped
//     individual toggle) — plus 4 bottom buttons at y=358.
//
// What ISN'T recovered from this data: which specific setting each of the
// 36 rows originally represented — there's no label text or per-row string
// table in either file, only geometry. This engine's own WC3Options struct
// (see WC3Options.h) has fewer distinct settings than there are layout
// rows anyway (the authentic WC3 options screen almost certainly covers
// things — joystick calibration detail, difficulty, etc. — this remake
// doesn't model). So this reuses the layout's real column positions/
// spacing/grouping *style* and the real background/button art, but places
// this engine's actual settings into those slots in its own order rather
// than claiming to reconstruct the authentic per-row mapping. Labels are
// still drawn with WC3Font (no label sprites exist in the VFX data to
// reuse instead).
class WC3OptionsActivity : public IActivity {
public:
    // options: the single shared settings struct (owned by whoever pushed
    // this activity — WC3GameFlow — so edits are visible immediately to
    // both the terminal and any future Alt+O reopen without re-loading).
    explicit WC3OptionsActivity(WC3Options* options);

    void init() override;
    void runFrame() override;

private:
    WC3Options* m_options;

    // (x,y,w,h) rects computed at layout time in runFrame(), reused for
    // click hit-testing — same pattern as WC3GameFlow's dialog overlays
    // (e.g. AWAITING_BRANCH_CHOICE's trueX/falseX rects).
    struct HitRect { int x, y, w, h; std::function<void()> onClick; };
    std::vector<HitRect> m_hitRects;

    // ESFOPTSD.DAT, decoded once in init().
    struct LayoutRow { int buttonIndex; int groupId; int x; int y; };
    std::vector<LayoutRow> m_layout;
    bool m_assetsLoaded{false};
    RSImageSet* m_background{nullptr};
    RSImageSet* m_buttonSprites{nullptr};
    VGAPalette m_palette;
    FrameBuffer* m_fb{nullptr};

    void loadAssets();

    // One checkbox/radio-choice/slider bound to a specific layout row,
    // built once per frame in layoutAndDraw() and consumed by two passes
    // (icon draw on the palette-indexed FrameBuffer, then label+hit-rect
    // on the RGBA buffer after palette conversion — RLEShape/FrameBuffer
    // and WC3Font::drawTextRGBA are two different pixel pipelines that
    // don't mix mid-buffer, so the row's icon and its label are drawn in
    // separate stages rather than together).
    struct Binding {
        size_t rowIndex;
        std::string label;
        bool checked;
        std::function<void()> onClick;  // checkbox: toggle. slider: decrease (left arrow).
        bool isSlider{false};
        std::function<void()> onClickAlt;  // slider only: increase (right arrow).
    };
    void layoutAndDraw();
};

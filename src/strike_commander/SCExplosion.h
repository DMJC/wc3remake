#pragma once
#include "precomp.h"

class SCExplosion {
private:
    SCRenderer &Renderer = SCRenderer::getInstance();
public:
    RSEntity *obj{nullptr};
    int fps{0};
    int current_frame{0};
    Vector3D position{0, 0, 0};
    bool is_finished{false};
    // Billboard size passed to Renderer.drawBillboard — 50.0f (the
    // pre-existing fighter-scale default) unless the caller overrides it.
    // Capital ships pass 12x this (see the IsCapitalShipName-gated spawn
    // sites in SCMissionActors.cpp/SCMission.cpp/SCProg.cpp) since a
    // fighter-sized explosion billboard reads as a barely-visible dot next
    // to a ship hundreds of units long (user-confirmed 2026-07 session,
    // raised from an initial 4x).
    float scale{50.0f};
    // Large-capital-ship death sequence (user-confirmed 2026-07 session):
    // play the first 2 frames, hold there while the screen goes white for
    // 0.5s, then resume for the remaining frames. bigDeathSequence is set
    // once at spawn (see the IsLargeCapitalShipName-gated sites in
    // SCMissionActors.cpp/SCMission.cpp/SCProg.cpp); the rest is internal
    // playback state. whiteoutRequested is a one-shot signal — true for
    // exactly the frame the hold begins, so the caller (SCStrike.cpp's
    // explosion loop) can kick off SCStrike::screen_whiteout_timer, then
    // read back false every frame after.
    bool bigDeathSequence{false};
    bool isPaused{false};
    float pauseTimer{0.0f};
    bool whiteoutRequested{false};

    void render();
    SCExplosion(RSEntity *obj, Vector3D position, float scale = 50.0f, bool bigDeathSequence = false);
};
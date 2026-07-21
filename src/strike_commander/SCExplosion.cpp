#include "precomp.h"
#include "../engine/gametimer.h"

void SCExplosion::render() {
    if (is_finished) return;
    if (this->isPaused) {
        this->pauseTimer -= GameTimer::getInstance().getDeltaTime();
        if (this->pauseTimer <= 0.0f) {
            // Resume straight into frame index 2 — advancing directly
            // here (rather than letting the normal fps-driven increment
            // below re-discover current_frame==1) is what keeps this a
            // one-shot pause instead of re-triggering every time fps next
            // crosses its threshold.
            this->isPaused = false;
            this->fps = 0;
            this->current_frame = 2;
        } else {
            // Hold on the 2nd frame (index 1) for the rest of the pause —
            // the screen-white overlay drawn elsewhere (SCStrike.cpp,
            // SCRenderer::renderFullscreenFlash) covers it anyway, so what's
            // drawn here doesn't matter much, but keep drawing rather than
            // going blank in case the overlay's own alpha ever changes.
            if (this->current_frame < (int)this->obj->animations.size()) {
                Renderer.drawBillboard(this->position, obj->animations[this->current_frame], this->scale);
            }
            return;
        }
    }
    this->fps++;
    if (this->fps > 6) {
        this->fps = 0;
        // Large-capital-ship death sequence (user-confirmed 2026-07
        // session): once the 2nd frame (index 1) has had its normal
        // display time, pause right there instead of advancing further,
        // and signal the caller to start a 0.5s screen whiteout.
        if (this->bigDeathSequence && this->current_frame == 1) {
            this->isPaused = true;
            this->pauseTimer = 0.5f;
            this->whiteoutRequested = true;
            Renderer.drawBillboard(this->position, obj->animations[this->current_frame], this->scale);
            return;
        }
        this->current_frame = this->current_frame + 1;
    }
    if (this->current_frame >= this->obj->animations.size()) {
        this->is_finished = true; // Mark as finished if no more frames
        return;
    }
    Renderer.drawBillboard(
        this->position,
        obj->animations[this->current_frame],
        this->scale
    );
}
SCExplosion::SCExplosion(RSEntity *obj, Vector3D position, float scale, bool bigDeathSequence)
    : obj(obj), fps(0), position(position), is_finished(false), scale(scale), bigDeathSequence(bigDeathSequence) {
    if (obj == nullptr) {
        is_finished = true; // If no object is provided, mark as finished
    }
}

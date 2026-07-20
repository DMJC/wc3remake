#pragma once
#include "precomp.h"

// A single flying wreckage piece spawned from RSEntity::debris (OBJT>...>
// DEBR) when a ship is destroyed — see SCMissionActors::hasBeenHit. Simple
// ballistic drift (no gravity — space combat), no rotation simulated yet
// (drawn at the parent ship's orientation at spawn time, held fixed).
class SCDebris {
private:
    SCRenderer &Renderer = SCRenderer::getInstance();
public:
    RSEntity *obj{nullptr};
    Vector3D position{0, 0, 0};
    Vector3D velocity{0, 0, 0};
    Vector3D orientation{0, 0, 0};
    // Seconds remaining before this piece despawns.
    float lifetime{6.0f};
    bool is_finished{false};

    void update(float dt);
    void render();
    SCDebris(RSEntity *obj, Vector3D position, Vector3D velocity, Vector3D orientation);
};

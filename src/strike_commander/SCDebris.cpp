#include "precomp.h"
#include "SCDebris.h"

SCDebris::SCDebris(RSEntity *obj, Vector3D position, Vector3D velocity, Vector3D orientation)
    : obj(obj), position(position), velocity(velocity), orientation(orientation) {
    if (obj == nullptr) {
        is_finished = true;
    }
}

void SCDebris::update(float dt) {
    if (is_finished) {
        return;
    }
    this->position = this->position + (this->velocity * dt);
    this->lifetime -= dt;
    if (this->lifetime <= 0.0f) {
        this->is_finished = true;
    }
}

void SCDebris::render() {
    if (is_finished || obj == nullptr) {
        return;
    }
    Renderer.drawModel(obj, this->position, this->orientation);
}

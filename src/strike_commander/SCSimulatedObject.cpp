#include "precomp.h"
#include "SCSimulatedObject.h"
#define GRAVITY 9.8f
#define DRAG_COEFFICIENT 0.010f  // Coefficient de traînée
#define CROSS_SECTIONAL_AREA 0.10f  // Section transversale (m^2)
#define LIFT_COEFFICIENT  11.0f
#define MAX_VELOCITY 2000.0f  // Vitesse maximale autorisée en m/s
#define RO_TO_SI 515.379f

SCSimulatedObject::SCSimulatedObject() {             

    this->elevationf = 0.0f;
    this->vx = 0.0f;
    this->vy = 0.0f;
    this->vz = 0.0f;

    this->azimuthf = 0.0f;
    this->elevationf = 0.0f;
    
    this->smoke_positions.clear();
    this->smoke_positions.reserve(20);

}
SCSimulatedObject::~SCSimulatedObject() {

}

void SCSimulatedObject::Render() {

    Vector3D position = { this->x, this->y, this->z };
    Vector3D orientaton = { this->azimuthf, this->elevationf, 0.0f };

    Renderer.drawModel(this->obj, position, orientaton);

    // Engine thrust cone (OBJT>MISL>AFTB, user-confirmed real chunk
    // meaning, 2026-07 session) — every WC3 missile/torpedo has one
    // except mines (WDAT::is_guided_flight == false), which drop and sit
    // stationary instead of flying, so they have no thrust to show a
    // flame for. Drawn at the missile's own body position/orientation
    // rather than composing in AFTB::positions' per-mount local offset
    // (unlike the ship-hull afterburner rendering in SCRenderer.cpp,
    // which runs inside that model's own already-open GL matrix — there's
    // no equivalent context here, and that offset was already flagged as
    // unconfirmed/low-confidence when parsed — parseREAL_OBJT_MISL_AFTB_
    // DATA's own comment). Always the model's highest LOD (full burn —
    // missiles don't have a throttle to scale it by, unlike a ship's
    // engine).
    bool isFlying = this->obj->wdat == nullptr || this->obj->wdat->is_guided_flight;
    if (isFlying && this->obj->afterburner != nullptr && this->obj->afterburner->objct != nullptr &&
        !this->obj->afterburner->objct->lods.empty()) {
        size_t lod = this->obj->afterburner->objct->lods.size() - 1;
        Renderer.drawModel(this->obj->afterburner->objct, lod, position, orientaton, true);
    }

    // missile_smoke_textures is an SC1-specific asset set that's never
    // loaded for WC3 missions -- guarded here (rather than only at the
    // load site) since this whole Render() path is shared between both
    // games; without this check, any WC3 missile with dynn_miss velocity
    // data (parsed straight off its own OBJT>MISL>DYNM chunk) crashed on
    // the very first frame it rendered (missile_smoke_textures[0] on an
    // empty vector).
    if (this->obj->dynn_miss != nullptr && this->obj->dynn_miss->velovity_m_per_sec > 0 &&
        !this->SmokeSet.missile_smoke_textures.empty()) {

        int cpt=0;
        int nb_smoke = (int) this->smoke_positions.size();
        int cpt_smoke = 0;
        for (auto pos: this->smoke_positions) {
            float alpha = (nb_smoke - cpt_smoke) / ((float) nb_smoke);
            cpt_smoke++;
            if (cpt >= (int)this->SmokeSet.missile_smoke_textures.size()) {
                cpt = 0;
            }
            Renderer.drawBillboard(pos, this->SmokeSet.missile_smoke_textures[cpt], 10, alpha);
        }
    }
}

void SCSimulatedObject::GetPosition(Vector3D *position) {
    position->x = this->x;
    position->y = this->y;
    position->z = this->z;
}
// Fonction pour convertir les coordonnées polaires en coordonnées cartésiennes
Vector3D polarToCartesian(float speed, float phi, float theta) {
    Vector3D v;
    v.x = speed * sinf(theta) * cosf(phi);
    v.y = speed * sinf(theta) * sinf(phi);
    v.z = speed * cosf(theta);
    return v;
}
#define EPSILON 1e-10 // Tolérance pour les très petits nombres
// Fonction pour convertir les coordonnées cartésiennes en coordonnées polaires
void cartesianToPolar(Vector3D v, float *phi, float *theta) {
    *phi = atan2f(v.x, v.z);
    *theta = acosf(v.y / sqrtf(v.x * v.x + v.y * v.y + v.z * v.z));
}

// Calcul de la norme d'un vecteur

// Calcul de la force de frottement
Vector3D SCSimulatedObject::calculate_drag(Vector3D velocity) {
    float speed_per_tick = velocity.Length();
    if (speed_per_tick < EPSILON) return { 0.0f, 0.0f, 0.0f };

    float speed_mps = speed_per_tick * tps;  // cohérent avec calculate_lift

    int itemp = ((int)this->y) >> 10;
    if (itemp > 74) itemp = 74;
    else if (itemp < 0) itemp = 0;

    float drag_magnitude = 0.5f * ro[itemp] * RO_TO_SI * speed_mps * speed_mps * DRAG_COEFFICIENT * CROSS_SECTIONAL_AREA;
    return velocity * (-drag_magnitude / speed_per_tick);
}
Vector3D SCSimulatedObject::calculate_lift(Vector3D velocity) {
    float speed_per_tick = velocity.Length();
    if (speed_per_tick < EPSILON) {
        return { 0.0f, 0.0f, 0.0f };
    }
    if (this->obj->entity_type == EntityType::bomb) {
        return { 0.0f, 0.0f, 0.0f };
    }
    // Direction perpendiculaire à la vitesse dans le plan vertical
    Vector3D vel_dir = velocity * (1.0f / speed_per_tick);
    float dot_y = vel_dir.y;
    Vector3D lift_dir = {
        -vel_dir.x * dot_y,
        1.0f - dot_y * dot_y,
        -vel_dir.z * dot_y
    };
    float lift_dir_len = lift_dir.Length();
    if (lift_dir_len < EPSILON) {
        return { 0.0f, 0.0f, 0.0f };
    }

    // Le lift compense exactement la gravité (indépendant de ro[] et de CL)
    float lift_magnitude = this->weight * GRAVITY;
    return lift_dir * (lift_magnitude / lift_dir_len);
}
std::tuple<Vector3D, Vector3D> SCSimulatedObject::ComputeTrajectory(int tps) {
    this->tps = tps;
    float deltaTime = 1.0f / (float)tps;

    float thrust = 0.0f;
    if (this->obj->dynn_miss != nullptr) {
        thrust = (float)this->obj->dynn_miss->velovity_m_per_sec * 1000.0f;
    }

    Vector3D position = { this->x, this->y, this->z };
    Vector3D velocity = { this->vx, this->vy, this->vz };

    // Mines (WDAT::is_guided_flight == false — user-confirmed real
    // behavior): fired from a missile hardpoint like any other missile,
    // but they drop from the ship and stay stationary instead of flying
    // out, waiting for an enemy to come near. No thrust/steer/drag/lift —
    // just hold position exactly where SCPlane::Shoot() spawned it. The
    // existing dumbfire-scan-every-actor collision path in Simulate()
    // (target == nullptr) already gives proximity detonation against any
    // in-range enemy for free once this returns zero velocity here.
    if (this->obj != nullptr && this->obj->wdat != nullptr && !this->obj->wdat->is_guided_flight) {
        return { position, { 0.0f, 0.0f, 0.0f } };
    }

    Vector3D to_target = { 0.0f, 0.0f, 0.0f };
    if (this->target != nullptr) {
        to_target.x = (float)this->target->object->position.x;
        to_target.y = (float)this->target->object->position.y;
        to_target.z = (float)this->target->object->position.z;
    }

    // --- Forces de base ---
    Vector3D gravity_force = { 0.0f, -this->weight * GRAVITY, 0.0f };
    Vector3D drag_force    = this->calculate_drag(velocity);
    Vector3D lift_force    = this->calculate_lift(velocity);

    
    // --- Guidage aérodynamique ---
    Vector3D thrust_force  = { 0.0f, 0.0f, 0.0f };
    Vector3D steer_force   = { 0.0f, 0.0f, 0.0f };

    float speed = velocity.Length();
    float speed_mps = speed * tps;  // Convertir la vitesse par tick en m/s pour les calculs aérodynamiques
    Vector3D vel_dir = velocity;
    vel_dir.Normalize();

    float lift_y = lift_force.y;
    float gravity_y = this->weight * GRAVITY;
    printf("[LIFT DEBUG] lift=%.2f < gravity=%.2f (deficit=%.2f) speed_mps=%.1f y=%.1f\n",
            lift_y, gravity_y, gravity_y - lift_y, speed_mps, this->y);
    if (this->guidance && this->target != nullptr && speed > 1.0f) {
        // Direction vers la cible
        Vector3D to_target_dir = (to_target - position);
        to_target_dir.Normalize();

        // Composante latérale : partie de (to_target_dir) perpendiculaire à la vitesse
        float dot = vel_dir.DotProduct(&to_target_dir);
        Vector3D lateral = to_target_dir - vel_dir * dot;
        float lateral_len = lateral.Length();

        if (lateral_len > 0.001f) {
            int itemp = ((int)this->y) >> 10;
            if (itemp > 74) {
                itemp = 74;
            } else if (itemp < 0) {
                itemp = 0;
            }

            // Force aérodynamique latérale ~ 0.5 * rho * v² * S_control
            // AERO_CONTROL_COEFF est l'efficacité des surfaces de contrôle
            const float AERO_CONTROL_COEFF = 5.0f;
            float dyn_pressure = 0.5f * ro[itemp] * speed_mps * speed_mps;
            float steer_magnitude;
            if (this->obj->entity_type == EntityType::bomb) {
                // Guidage direct (ordinateur de bord) — indépendant de ro[]
                const float MAX_G_BOMB = 10.0f;
                steer_magnitude = this->weight * MAX_G_BOMB * GRAVITY;
            } else {
                // Force aérodynamique pour les missiles
                const float AERO_CONTROL_COEFF = 15.0f;
                float dyn_pressure = 0.5f * ro[itemp] * RO_TO_SI * speed_mps * speed_mps;
                steer_magnitude = dyn_pressure * AERO_CONTROL_COEFF * CROSS_SECTIONAL_AREA;
                const float MAX_G = 50.0f;
                float max_steer = this->weight * MAX_G * GRAVITY;
                if (steer_magnitude > max_steer) {
                    steer_magnitude = max_steer;
                }
            }

            // Limite en g (ex: 20g max)
            const float MAX_G = 50.0f;
            float max_steer = this->weight * MAX_G * GRAVITY;
            if (steer_magnitude > max_steer) steer_magnitude = max_steer;

            steer_force = lateral * (steer_magnitude / lateral_len);
        }

        // Poussée moteur : le long de la vitesse (forward), pas vers la cible
        thrust_force = vel_dir * thrust;

    } else {
        // Pas de guidage : poussée dans la direction de la vitesse
        thrust_force = vel_dir * thrust;
    }

    Vector3D total_force = gravity_force + drag_force + lift_force + thrust_force + steer_force;
    Vector3D acceleration = total_force * (1.0f / this->weight);
    velocity = (velocity + (acceleration * deltaTime)).limit(MAX_VELOCITY * deltaTime);
    position = position + velocity;

    this->run_iterations++;
    return { position, velocity };
}
// Slab-method swept segment-vs-AABB test (both in the same space — callers
// pass a target-relative segment against the target's own local bounding
// box). Standard algorithm (Kay/Kajiya slab test): clip the segment's
// parametric [0,1] range against each axis's pair of planes in turn: the
// segment hits the box iff the intersection of all three per-axis ranges
// is non-empty.
static bool SegmentIntersectsAABB(const Vector3D &a, const Vector3D &b, const Point3D &boxMin, const Point3D &boxMax) {
    Vector3D d = { b.x - a.x, b.y - a.y, b.z - a.z };
    float axisOrigin[3] = { a.x, a.y, a.z };
    float axisDir[3] = { d.x, d.y, d.z };
    float axisMin[3] = { boxMin.x, boxMin.y, boxMin.z };
    float axisMax[3] = { boxMax.x, boxMax.y, boxMax.z };
    float tmin = 0.0f, tmax = 1.0f;
    for (int i = 0; i < 3; i++) {
        if (fabsf(axisDir[i]) < 1e-6f) {
            if (axisOrigin[i] < axisMin[i] || axisOrigin[i] > axisMax[i]) {
                return false;
            }
            continue;
        }
        float invDir = 1.0f / axisDir[i];
        float t1 = (axisMin[i] - axisOrigin[i]) * invDir;
        float t2 = (axisMax[i] - axisOrigin[i]) * invDir;
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        tmin = (std::max)(tmin, t1);
        tmax = (std::min)(tmax, t2);
        if (tmin > tmax) {
            return false;
        }
    }
    return true;
}
// Shortest distance from point p to the segment [a,b] — used as the
// fallback hit test for targets with no real bounding box data.
static float PointToSegmentDistance(const Vector3D &p, const Vector3D &a, const Vector3D &b) {
    Vector3D ab = { b.x - a.x, b.y - a.y, b.z - a.z };
    Vector3D ap = { p.x - a.x, p.y - a.y, p.z - a.z };
    float abLenSq = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z;
    float t = (abLenSq > 1e-9f) ? ((ap.x * ab.x + ap.y * ab.y + ap.z * ab.z) / abLenSq) : 0.0f;
    t = (std::max)(0.0f, (std::min)(1.0f, t));
    Vector3D closest = { a.x + ab.x * t, a.y + ab.y * t, a.z + ab.z * t };
    Vector3D diff = { p.x - closest.x, p.y - closest.y, p.z - closest.z };
    return sqrtf(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
}
// User-direction (2026-07 session, "calculate bolt intersection"): was a
// single-point test against this frame's *current* position only — a
// fast-moving bolt or missile could tunnel straight through a target
// between two simulation ticks without either tick's sampled point ever
// landing inside the target's box/threshold. Now sweeps the whole segment
// travelled since the previous tick (last_px/py/pz -> x/y/z) against the
// target's bounding box (or, lacking one, the closest distance from the
// target to that swept segment rather than just to its endpoint).
// Applies identically regardless of which side is shooting, so player,
// friendly, and hostile targets are all hit through this same one path
// (see the self-exclusion-only comment below on the team check removal).
bool SCSimulatedObject::CheckCollision(SCMissionActors *entity) {
    // User-confirmed (2026-07 session): WC3 has real friendly fire (shooting
    // your own wingmen has actual gameplay consequences), so same-team
    // targets are no longer skipped here — only the shooter itself is
    // excluded, to stop a bolt/missile from colliding with its own origin.
    if (entity == shooter) {
        return false;
    }
    Vector3D segStart = { this->last_px, this->last_py, this->last_pz };
    Vector3D segEnd = { this->x, this->y, this->z };
    Vector3D targetPos = {
        static_cast<float>(entity->object->position.x),
        static_cast<float>(entity->object->position.y),
        static_cast<float>(entity->object->position.z)
    };
    BoudingBox *bb = entity->object->entity->GetBoudingBpx();
    if (bb != nullptr) {
        // Bounding box is target-local — move the segment into that same
        // local space instead of offsetting the box into world space.
        Vector3D localStart = { segStart.x - targetPos.x, segStart.y - targetPos.y, segStart.z - targetPos.z };
        Vector3D localEnd = { segEnd.x - targetPos.x, segEnd.y - targetPos.y, segEnd.z - targetPos.z };
        if (SegmentIntersectsAABB(localStart, localEnd, bb->min, bb->max)) {
            return true;
        }
    }
    const float distanceThreshold = 50.0f;
    return PointToSegmentDistance(targetPos, segStart, segEnd) < distanceThreshold;
}
// Explosion visual (OBJT>MISL>EXPL, user-confirmed real chunk meaning,
// 2026-07 session) + impact sound for an actual hit — factored out since
// Simulate() needs it from both hit-detection branches below (locked-
// target and dumbfire-scan-every-actor) as well as its own pre-existing
// expiry/ground-hit branches further down.
static void PlayImpactEffects(SCMission *mission, RSMixer &Mixer, RSEntity *obj, Vector3D position) {
    if (obj->explos != nullptr && obj->explos->objct != nullptr) {
        mission->explosions.push_back(new SCExplosion(obj->explos->objct, position));
    }
    if (mission->sound.sounds.size() > 0) {
        MemSound *sound = (obj->entity_type == EntityType::tracer)
            ? mission->sound.sounds[SoundEffectIds::GUN_IMPACT_1]
            : mission->sound.sounds[SoundEffectIds::EXPLOSION_1];
        Mixer.playSoundVoc(sound->data, sound->size);
    }
}
void SCSimulatedObject::Simulate(int tps) {
    Vector3D position, velocity;
    std::tie(position, velocity) = this->ComputeTrajectory(tps);

    if (this->target != nullptr && !this->is_simulated) {
        if (this->CheckCollision(this->target)) {
            this->alive = false;
            this->target->hasBeenHit(this, this->shooter);
            if (!this->is_simulated) {
                PlayImpactEffects(this->mission, Mixer, this->obj, position);
            }
            return;
        }
    } else {
        for (auto entity: this->mission->actors) {
            if (this->CheckCollision(entity)) {
                entity->hasBeenHit(this, this->shooter);
                this->alive = false;
                if (!this->is_simulated) {
                    PlayImpactEffects(this->mission, Mixer, this->obj, position);
                }
                break;
            }
        }
    }
    this->smoke_positions.insert(this->smoke_positions.begin(), position);
    if (this->smoke_positions.size() > 20) {
        this->smoke_positions.pop_back();
    }
    
    
    float azimut = 0.0f;
    float elevation = 0.0f;
    cartesianToPolar(velocity, &azimut, &elevation);
    this->azimuthf = (float)(azimut - M_PI_2);
    this->elevationf = (float)(M_PI_2-elevation);
    this->vx = velocity.x;
    this->vy = velocity.y;
    this->vz = velocity.z;
    this->last_px = this->x;
    this->last_py = this->y;
    this->last_pz = this->z;
    this->x = position.x;
    this->y = position.y;
    this->z = position.z;

    Vector3D length_vect = { this->x - this->last_px, this->y - this->last_py, this->z - this->last_pz };
    this->distance += length_vect.Length();
    if (this->distance > this->obj->wdat->effective_range && this->obj->dynn_miss != nullptr) {
        this->alive = false;
        if (!this->is_simulated) {
            this->mission->explosions.push_back(new SCExplosion(this->obj->explos->objct, position));
            if (this->mission->sound.sounds.size() > 0) {
                MemSound *sound;
                if (this->obj->entity_type == EntityType::tracer) {
                    sound = this->mission->sound.sounds[SoundEffectIds::GUN_IMPACT_1];
                } else {
                    sound = this->mission->sound.sounds[SoundEffectIds::EXPLOSION_1];
                }
                Mixer.playSoundVoc(sound->data, sound->size);
            }
        }
    } else if (this->distance > this->obj->wdat->effective_range && !no_gravity) {
        if (this->obj->dynn_miss != nullptr) {
            this->obj->dynn_miss->velovity_m_per_sec = 0;
        }
    }
    // this->mission->area is only allocated for atmospheric missions
    // (WC3Mission.cpp: `if (!this->is_space_mission) { this->area = new
    // RSArea(); ... }`) -- stays null for space missions, which is most
    // WC3 combat. This was unconditionally dereferenced with no null
    // check, so every projectile fired in a space mission hit undefined
    // behavior on its very first simulation tick (there's no ground to
    // collide with in space anyway, so skipping this check entirely is
    // correct, not just crash-safe).
    if (this->mission->area != nullptr && this->y < this->mission->area->getY(this->x, this->z)) {
        if (!this->is_simulated) {
            // WC3 synthetic weapon entities (RSEntity::getWC3SyntheticWeapon)
            // carry no explos data (no on-disk asset to source it from) --
            // guarded rather than crashing; just skip the visual explosion.
            if (this->obj->explos != nullptr) {
                this->mission->explosions.push_back(new SCExplosion(this->obj->explos->objct, position));
            }
            if (this->mission->sound.sounds.size() > 0) {
                MemSound *sound;
                sound = this->mission->sound.sounds[SoundEffectIds::EXPLOSION_1];
                Mixer.playSoundVoc(sound->data, sound->size);
            }
            for (auto entity: this->mission->actors) {
                if (this->CheckCollision(entity)) {
                    entity->hasBeenHit(this, this->shooter);
                }
            }
        }
        this->alive = false;
    }
}
std::tuple<Vector3D, Vector3D> GunSimulatedObject::ComputeTrajectory(int tps) {
    float deltaTime = 1.0f / static_cast<float>(tps);
    Vector3D position = { this->x, this->y, this->z };
    Vector3D velocity = { this->vx, this->vy, this->vz };

    // Calcul de la force de gravité (en utilisant la constante GRAVITY et le poids de l'objet)
    // 
    if (this->weight == 0) {
        this->weight = 0.2f;
    }
    Vector3D gravity_force = { 0.0f, -this->weight * GRAVITY , 0.0f };

    // Calcul de la force de frottement (drag) avec une densité d'air standard (1.225 kg/m^3)
    float speed = velocity.Length();
    Vector3D drag_force = { 0.0f, 0.0f, 0.0f };
    if (speed > EPSILON) {
        float air_density = 1.225f;
        float drag_magnitude = 0.005f * air_density * speed * speed * DRAG_COEFFICIENT * CROSS_SECTIONAL_AREA;
        drag_force = velocity * (-drag_magnitude / speed);
    }

    // Calcul de l'accélération à partir des forces appliquées (a = F/m)
    
    Vector3D acceleration = (gravity_force + drag_force) * (1.0f / this->weight);

    // Mise à jour de la vitesse et de la position en tenant compte du temps écoulé
    velocity = velocity + acceleration * deltaTime;
    position = position + velocity * deltaTime;
    this->run_iterations++;
    return { position, velocity };
}
std::tuple<Vector3D, Vector3D> GunSimulatedObject::ComputeTrajectoryUntilGround(int tps) {

    Vector3D position{0,0,0};
    Vector3D velocity{0,0,0};
    Vector3D oldpos{0,0,0};
    std::tie(position, velocity) = this->ComputeTrajectory(tps);
    int cpt_iteration = 0;
    while (position.y > this->mission->area->getY(position.x, position.z) == true && cpt_iteration<100000) {
        oldpos = position;
        std::tie(position, velocity) = this->ComputeTrajectory(tps);
        this->x = position.x;
        this->y = position.y;
        this->z = position.z;
        this->vx = velocity.x;
        this->vy = velocity.y;
        this->vz = velocity.z;
        if (oldpos.x == position.x && oldpos.y == position.y && oldpos.z == position.z && cpt_iteration>1000) {
            printf("should not happen\n");
            break;
        }
        cpt_iteration++;
    }
    return { position, velocity };
}
void GunSimulatedObject::Simulate(int tps) {
    
    // Actualisation des attributs de l'objet
    Vector3D position;
    Vector3D velocity;
    std::tie(position, velocity) = this->ComputeTrajectory(tps);
    float azimut = 0.0f;
    float elevation = 0.0f;
    cartesianToPolar(velocity, &azimut, &elevation);
    this->azimuthf = (float)(azimut - M_PI_2);
    this->elevationf = (float)(M_PI_2-elevation);
    this->vx = velocity.x;
    this->vy = velocity.y;
    this->vz = velocity.z;
    this->last_px = this->x;
    this->last_py = this->y;
    this->last_pz = this->z;
    this->x = position.x;
    this->y = position.y;
    this->z = position.z;

    Vector3D length_vect = { this->x - this->last_px, this->y - this->last_py, this->z - this->last_pz };
    this->distance += length_vect.Length();
    // User-direction (2026-07 session): dropped the unexplained x10
    // multiplier that used to sit on top of effective_range here --
    // effective_range is already real per-weapon data (RSEntity::
    // parseREAL_OBJT_GUNS_DATA, confirmed exact against real Tachyon Gun
    // stats: 3200), so this now expires a bolt at its actual real range.
    if (this->distance > this->obj->wdat->effective_range && this->obj->entity_type == EntityType::tracer) {
        this->alive = false;
    }
    if (!this->is_simulated) {
        for (auto entity: this->mission->actors) {
            if (this->CheckCollision(entity)) {
                entity->hasBeenHit(this, this->shooter);
                this->alive = false;
                break;
            }
        }
        // See the same guard's own comment in SCSimulatedObject::Simulate()
        // above -- mission->area is null for space missions.
        if (this->mission->area != nullptr && this->y < this->mission->area->getY(this->x, this->z)) {
            this->alive = false;
            if (this->obj->wdat->radius > 5 && this->obj->entity_type == EntityType::bomb) {
                for (auto entity : this->mission->actors) {
                    float distance = entity->object->position.Distance(&position);
                    if (!entity->object->alive) {
                        continue;
                    }
                    if (distance < this->obj->wdat->radius) {
                        entity->hasBeenHit(this, this->shooter);
                    }
                }
            }
            if (this->obj->explos != nullptr && this->obj->explos->objct != nullptr) {
                SCExplosion *explosion = new SCExplosion(this->obj->explos->objct, position);
                this->mission->explosions.push_back(explosion);
                if (this->obj->entity_type != EntityType::tracer) {
                    if (this->mission->sound.sounds.size() > 0) {
                        MemSound *sound = this->mission->sound.sounds[SoundEffectIds::EXPLOSION_3];
                        Mixer.playSoundVoc(sound->data, sound->size);
                    }
                }
            }
        }
    }
}

// Per-weapon bolt tint. WC3's own weapon data carries no color field --
// APPR>LASR>INFO is byte-identical between differently-colored weapons
// (confirmed: LASER vs MESOGUN) -- so these are hardcoded from the real
// in-game bolt colors (user-supplied). Keyed on RSEntity::weapon_display_name
// (the GUNS>DATA display name, e.g. "Laser", "Neutron Gun"), which is exact
// and unambiguous, unlike the .IFF filename (5 separate files are all named
// "Laser": LASER/RLASER/TURLASER/YLASER/CYLASER).
static Vector3D getGunBoltColor(const std::string &weaponDisplayName) {
    if (weaponDisplayName == "Laser") return {227.0f / 255.0f, 16.0f / 255.0f, 16.0f / 255.0f};
    if (weaponDisplayName == "Ion Gun") return {239.0f / 255.0f, 16.0f / 255.0f, 239.0f / 255.0f};
    if (weaponDisplayName == "Neutron Gun") return {239.0f / 255.0f, 130.0f / 255.0f, 48.0f / 255.0f};
    if (weaponDisplayName == "Plasma Gun") return {32.0f / 255.0f, 203.0f / 255.0f, 166.0f / 255.0f};
    if (weaponDisplayName == "Meson Gun") return {73.0f / 255.0f, 215.0f / 255.0f, 20.0f / 255.0f};
    if (weaponDisplayName == "Photon Gun") return {219.0f / 255.0f, 227.0f / 255.0f, 24.0f / 255.0f};
    if (weaponDisplayName == "Reaper Gun") return {239.0f / 255.0f, 16.0f / 255.0f, 239.0f / 255.0f};
    if (weaponDisplayName == "Tachyon Gun") return {195.0f / 255.0f, 199.0f / 255.0f, 207.0f / 255.0f};
    return {1.0f, 1.0f, 0.0f}; // unmapped weapon: previous default bolt color
}

void GunSimulatedObject::Render() {
    Vector3D pos = {this->x, this->y, this->z};
    Vector3D orient = {this->azimuthf, this->elevationf, 0.0f};
    if (this->obj->vertices.size() == 0) {
        Renderer.drawBolt(pos, orient, getGunBoltColor(this->obj->weapon_display_name));
    } else {
        Renderer.drawModel(this->obj, pos, orient);
    }
}

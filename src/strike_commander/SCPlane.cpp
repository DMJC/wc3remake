//
//  SCPlane.cpp
//  libRealSpace
//
//  Created by Rémi LEONARD on 31/08/2024.
//  Copyright (c) 2014 Fabien Sanglard. All rights reserved.
//
#include "precomp.h"
#include "SCWeaponPredictor.h"
#include <map>
void cartesianToPolar(Vector3D v, float *phi, float *theta);
// Définition des constantes physiques
const float GRAVITY = 9.81f; // m/s^2
const float AIR_DENSITY = 1.225f; // kg/m^3
const float DRAG_COEFFICIENT = 0.47f;
const float LIFT_COEFFICIENT = 0.5f; // Doit être ajusté en fonction de la forme de l'objet
const float WING_AREA = 1.0f; // m^2

/**
 * \brief Computes sine and cosine of angle a, given in 10th of degree.
 *
 * \param a angle in 10th of degree.
 * \param b pointer to float where the sine of a will be stored (if not null).
 * \param c pointer to float where the cosine of a will be stored (if not null).
 */
void gl_sincos(float a, float *b, float *c) {
    if (b != NULL) {
        *b = sinf(tenthOfDegreeToRad(a));
    }
    if (c != NULL) {
        *c = cosf(tenthOfDegreeToRad(a));
    }
}
/**
 * Returns a random number between 0 and maxr (inclusive) using a
 * simple linear congruential generator. The seed is initialized to
 * 1 at startup and is advanced every time the function is called.
 *
 * @param maxr The maximum value that can be returned.
 *
 * @returns A random number between 0 and maxr (inclusive). If maxr
 *          is odd, the returned value can be negative.
 */
int mrandom(int maxr) {
    static unsigned long randx = 1;
    int n, retval;

    for (n = 1; n < 32 && (1 << n) < maxr; n++)
        ;

    retval = maxr << 1;
    while (retval > maxr) {
        randx = randx * 1103515245 + 12345;
        retval = (randx & 0x7fffffff) >> (31 - n);
    }
    randx = randx * 1103515245 + 12345;
    if (randx & 0x40000000)
        return (retval);
    else
        return (-retval);
}

// Ajouter ces méthodes

Vector3D SCPlane::PredictShot(int weapon_hard_point_id, SCMissionActors *target) {
    if (this->weaps_load[weapon_hard_point_id] == nullptr) {
        return {0, 0, 0};
    }
    
    // Initialiser le prédicteur si nécessaire
    if (!this->weapon_predictor) {
        this->weapon_predictor = new SCWeaponPredictor();
    }
    
    // Calcul de la direction et vitesse initiale comme dans la méthode Shoot()
    Vector3D initial_trust = {0, 0, 0};
    Vector3D direction = {
        this->x - this->last_px, this->y - this->last_py,
        this->z - this->last_pz
    };
    
    if (this->pilot != nullptr && this->pilot->actor_name != "PLAYER") {
        if (target->plane != nullptr) {
            direction = {
                target->plane->x - this->x, target->plane->y - this->y,
                target->plane->z - this->z
            };
        }
    }
    
    
    // Ajuster le thrust selon le type d'arme
    switch (this->weaps_load[weapon_hard_point_id]->objct->wdat->weapon_id) {
        case weapon_ids::ID_20MM:
            initial_trust = this->getWeaponIntialVector(1000.0f);
            break;
        case weapon_ids::ID_MK20:
        case weapon_ids::ID_MK82:
        case weapon_ids::ID_DURANDAL:
            initial_trust = this->getWeaponIntialVector(1.0f);
            break;
        default:
            initial_trust = this->getWeaponIntialVector(1.0f);
            break;
    }
    float thrustMagnitude = initial_trust.Length();
    
    // Prédiction pour les tirs IA comme dans Shoot()
    if (this->pilot != nullptr && this->pilot->actor_name != "PLAYER") {
        Vector3D targetVelocity = {0, 0, 0};
        Vector3D predictedPosition = { 0, 0, 0 };
        float distance = direction.Length();
        float timeToTarget = distance / -thrustMagnitude;

        if (target->plane != nullptr) {
            targetVelocity = {
                target->plane->x - target->plane->last_px,
                target->plane->y - target->plane->last_py,
                target->plane->z - target->plane->last_pz
            };
            predictedPosition = {
                target->plane->x + targetVelocity.x * timeToTarget,
                target->plane->y + targetVelocity.y * timeToTarget,
                target->plane->z + targetVelocity.z * timeToTarget
            };
        } else {
            predictedPosition = {
                target->object->position.x,
                target->object->position.y,
                target->object->position.z
            };
        }
        
        direction = {
            predictedPosition.x - this->x,
            predictedPosition.y - this->y,
            predictedPosition.z - this->z
        };
        
        // Affiner la prédiction
        for (int i = 0; i < 3; i++) {
            distance = direction.Length();
            timeToTarget = distance / -thrustMagnitude;
            
            if (target->plane != nullptr) {
                predictedPosition = {
                    target->plane->x + targetVelocity.x * timeToTarget,
                    target->plane->y + targetVelocity.y * timeToTarget,
                    target->plane->z + targetVelocity.z * timeToTarget
                };
            }
            
            direction = {
                predictedPosition.x - this->x,
                predictedPosition.y - this->y,
                predictedPosition.z - this->z
            };
        }
        
        direction.Normalize();
        initial_trust = direction * -thrustMagnitude;
    }
    
    // Position initiale
    Vector3D position = {this->x, this->y, this->z};
    
    // Prédire la trajectoire en utilisant l'objet de simulation réel
    Vector3D adjusted_direction = this->weapon_predictor->PredictTrajectory(
        this->weaps_load[weapon_hard_point_id]->objct,
        this->pilot,
        target,
        position,
        initial_trust,
        this->pilot->mission
    );
    
    // Activer l'affichage de la trajectoire
    this->is_showing_trajectory = true;
    this->trajectory_display_counter = 0;
    
    return adjusted_direction;
}

bool SCPlane::ShootWithPrediction(int weapon_hard_point_id, SCMissionActors *target, SCMission *mission, float lockProgressSeconds) {
    // Prédire d'abord le tir
    Vector3D adjusted_direction = this->PredictShot(weapon_hard_point_id, target);

    // Vérifier que l'arme est disponible
    if (this->weaps_load[weapon_hard_point_id]->nb_weap <= 0) {
        weapon_hard_point_id = (int) this->object->entity->hpts.size() - weapon_hard_point_id;
        if (weapon_hard_point_id <= 0 ||
            weapon_hard_point_id >= this->weaps_load.size() ||
            this->weaps_load[weapon_hard_point_id] == nullptr ||
            this->weaps_load[weapon_hard_point_id]->nb_weap == 0) {
            return false;
        }
    }

    // Si en cooldown, ne pas tirer
    if (this->wp_cooldown > 0) {
        this->wp_cooldown--;
        return false;
    }
    
    // WC3 torpedo/T-bomb (weapon_category==3): same "refuse to fire below
    // the weapon's own real lock_time_required_seconds" rule as the main
    // Shoot() switch — AI callers pass the 999.0f sentinel default so
    // this never actually blocks them (see Shoot()'s own comment on why
    // AI doesn't need a real lock timer), but a player-fired predictive
    // shot (not currently reachable — prediction is AI-only, gated in
    // Shoot()) would still be gated correctly if that ever changes.
    {
        auto *checkWdat = this->weaps_load[weapon_hard_point_id]->objct->wdat;
        if (checkWdat != nullptr && checkWdat->weapon_category == 3) {
            if (lockProgressSeconds < checkWdat->lock_time_required_seconds) {
                return false;
            }
        }
    }

    // Créer l'objet réel en utilisant les paramètres ajustés
    SCSimulatedObject *weap = nullptr;
    MemSound *sound = nullptr;

    switch (this->weaps_load[weapon_hard_point_id]->objct->wdat->weapon_id) {
        case weapon_ids::ID_20MM:
        case weapon_ids::ID_NEUTGUN:
        case weapon_ids::ID_IONGUN:
        case weapon_ids::ID_RLASER_WC3:
        case weapon_ids::ID_REAPGUN:
        case weapon_ids::ID_TACHGUN:
            weap = new GunSimulatedObject();
            this->wp_cooldown = 30;
            break;
        case weapon_ids::ID_MK20:
        case weapon_ids::ID_MK82:
        case weapon_ids::ID_DURANDAL:
            weap = new GunSimulatedObject();
            if (this->pilot->mission->sound.sounds.size() > 0) {
                sound = this->pilot->mission->sound.sounds[SoundEffectIds::MK82_DROP];
                Mixer.playSoundVoc(sound->data, sound->size);
            }
            this->wp_cooldown = 120;
            break;
        default:
            if (this->pilot->mission->sound.sounds.size() > 0) {
                sound = this->pilot->mission->sound.sounds[SoundEffectIds::AIM9_SHOOT];
                Mixer.playSoundVoc(sound->data, sound->size);
            }
            weap = new SCSimulatedObject();
            this->wp_cooldown = 160;
            break;
    }
    // Calcul de la direction et vitesse initiale avec ajustement
    Vector3D initial_trust = {0, 0, 0};
    Vector3D direction = {
        this->x - this->last_px, this->y - this->last_py,
        this->z - this->last_pz
    };
    float planeSpeed = direction.Length();
    
    float thrustMagnitude = planeSpeed;
    switch (this->weaps_load[weapon_hard_point_id]->objct->wdat->weapon_id) {
        case weapon_ids::ID_20MM: // Gun
        case weapon_ids::ID_NEUTGUN:
        case weapon_ids::ID_IONGUN:
        case weapon_ids::ID_RLASER_WC3:
        case weapon_ids::ID_REAPGUN:
        case weapon_ids::ID_TACHGUN:
            thrustMagnitude = planeSpeed * 250.0f * (this->tps / 60.0f);
            break;
        case weapon_ids::ID_MK20:
        case weapon_ids::ID_MK82: // Bombs
            thrustMagnitude = planeSpeed * 50.0f * (this->tps / 60.0f);
            break;
        default: // Missiles
            thrustMagnitude = planeSpeed * 100.0f * (this->tps / 60.0f);
            break;
    }
    
    // Appliquer la direction ajustée
    initial_trust = adjusted_direction * thrustMagnitude;
    
    // Configurer l'objet d'arme
    weap->mission = mission;
    weap->shooter = this->pilot;
    weap->obj = this->weaps_load[weapon_hard_point_id]->objct;
    weap->x = this->x;
    weap->y = this->y;
    weap->z = this->z;
    
    // Calculer l'orientation à partir de la direction
    float azimut = 0.0f;
    float elevation = 0.0f;
    cartesianToPolar(initial_trust, &azimut, &elevation);
    weap->azimuthf = (float)(azimut - M_PI_2);
    weap->elevationf = (float)(M_PI_2-elevation);
    
    weap->vx = initial_trust.x;
    weap->vy = initial_trust.y;
    weap->vz = initial_trust.z;
    weap->weight = this->weaps_load[weapon_hard_point_id]->objct->weight_in_kg * 2.205f;
    weap->target = target;
    
    // Décrémenter le nombre d'armes disponibles
    this->weaps_load[weapon_hard_point_id]->nb_weap--;

    // Ajouter à la liste des objets simulés
    this->weaps_object.push_back(weap);
    return true;
}

void SCPlane::RenderWeaponTrajectories() {
    // Afficher la trajectoire si nécessaire
    if (this->is_showing_trajectory && this->weapon_predictor) {
        this->weapon_predictor->RenderTrajectory();
        
        // Compteur pour limiter la durée d'affichage
        this->trajectory_display_counter++;
        if (this->trajectory_display_counter > 30) { // 1 seconde à 30 FPS
            this->is_showing_trajectory = false;
        }
    }
}

/**
 * SCPlane constructor. Initializes all members to 0.
 *
 * SCPlane is a structure that represents a plane in the game.
 *
 * @see SCPlane
 */
SCPlane::SCPlane() {
    this->alive = 0;
    this->status = 0;
    this->x = 0.0f;
    this->y = 0.0f;
    this->z = 0.0f;
    this->twist = 0;
    this->roll_speed = 0;
    this->azimuthf = 0.0f;
    this->elevationf = 0.0f;
    this->elevation_speedf = 0.0f;
    this->azimuth_speedf = 0.0f;
    this->airspeed = 0;
    this->thrust = 0;
    this->rollers = 0.0f;
    this->rudder = 0.0f;
    this->elevator = 0.0f;
    this->object = nullptr;
}
/**
 * Constructor for SCPlane.
 *
 * @param LmaxDEF the maximum lift coefficient.
 * @param LminDEF the minimum lift coefficient.
 * @param Fmax the maximum flap deflection.
 * @param Smax the maximum spoiler deflection.
 * @param ELEVF_CSTE the elevator rate in degrees per second.
 * @param ROLLFF_CSTE the roll rate in degrees per second.
 * @param s the wing span.
 * @param W the plane weight.
 * @param fuel_weight the fuel weight.
 * @param Mthrust the maximum thrust.
 * @param b the wing aspect ratio.
 * @param ie_pi_AR the inverse of the aspect ratio times pi.
 * @param MIN_LIFT_SPEED the minimum lift speed.
 * @param area the terrain area.
 * @param x the initial x position.
 * @param y the initial y position.
 * @param z the initial z position.
 */
SCPlane::SCPlane(float LmaxDEF, float LminDEF, float Fmax, float Smax, float ELEVF_CSTE, float ROLLFF_CSTE, float s,
                 float W, float fuel_weight, float Mthrust, float b, float ie_pi_AR, int MIN_LIFT_SPEED, 
                 RSArea *area, float x, float y, float z) {
    this->weaps_load.reserve(9);
    this->weaps_load.resize(9);
    this->alive = 0;
    this->status = 0;
    this->x = 0.0f;
    this->y = 0.0f;
    this->z = 0.0f;
    this->twist = 0;
    this->roll_speed = 0;
    this->azimuthf = 0.0f;
    this->elevationf = 0.0f;
    this->elevation_speedf = 0.0f;
    this->azimuth_speedf = 0.0f;
    this->airspeed = 0;
    this->thrust = 0;
    this->rollers = 0.0f;
    this->rudder = 0.0f;
    this->elevator = 0.0f;
    this->object = nullptr;

    this->LmaxDEF = LmaxDEF;
    this->LminDEF = LminDEF;
    this->Fmax = Fmax;
    this->Smax = Smax;
    this->ELEVF_CSTE = ELEVF_CSTE;
    this->ROLLFF_CSTE = ROLLFF_CSTE;
    this->s = s;
    this->W = W;
    this->fuel_weight = fuel_weight;
    this->Mthrust = Mthrust;
    this->b = b;
    this->ie_pi_AR = ie_pi_AR;
    this->MIN_LIFT_SPEED = MIN_LIFT_SPEED;
    this->object = nullptr;
    this->area = area;
    this->tps = 30;
    this->last_time = SDL_GetTicks();
    this->tick_counter = 0;
    this->last_tick = 0;
    this->x = x;
    this->y = y;
    this->z = z;
    this->ro2 = .5f * ro[0];
    init();
}
SCPlane::~SCPlane() {
    for (auto smoke: this->smoke_set->textures) {
        glDeleteTextures(1, &smoke->id);
        smoke->initialized = false;
    }
    if (this->weapon_predictor) {
        delete this->weapon_predictor;
        this->weapon_predictor = nullptr;
    }
}
/**
 * Initializes the SCPlane object by setting the default values for all its properties.
 */
void SCPlane::init() {

    this->twist = 0;

    this->status = 580000;
    this->alive = this->tps << 2;
    this->flaps = 0;
    this->spoilers = 0;
    this->rollers = 0;
    this->rudder = 0;
    this->elevator = 0;
    /* elevator rate in degrees/sec	*/
    this->ELEVF = this->ELEVF_CSTE * 10.0f / (20.0f * 400.0f);
    /* roll rate (both at 300 mph)	*/
    this->ROLLF = this->ROLLFF_CSTE * 10.0f / (30.0f * 400.0f);
    /* coefficient of parasitic drag*/
    this->Cdp = .015f;
    /* 1.0 / pi * wing Aspect Ratio	*/
    this->ipi_AR = 1.0f / ((float)M_PI * this->b * this->b / this->s);
    /* 1.0 / pi * AR * efficiency	*/
    this->ie_pi_AR = this->ipi_AR / this->ie_pi_AR;
    this->gravity = G_ACC / this->tps / this->tps;
    this->fps_knots = this->tps * (3600.0f / 6082.0f);
    this->Lmax = this->LmaxDEF * this->gravity;
    this->Lmin = this->LminDEF * this->gravity;
    this->wheels = 1;
    this->Cdp *= 2.0;
    this->fuel = 100 << 7;
    this->gefy = .7f * this->b;
    this->thrust = 0;
    this->tilt_factor = 0.17f;
    this->roll_speed = 0;
    this->azimuth_speedf = 0.0f;
    this->elevation_speedf = 0.0f;
    this->elevationf = 0.0f;
    this->inverse_mass = G_ACC / (this->W + this->fuel / 12800.0f * this->fuel_weight);
    this->on_ground = 1;
    this->vx = 0.0f;
    this->vy = 0.0f;
    this->vz = 0.0f;

    this->az = 0.0f;
    this->ay = 0.0f;
    this->ax = 0.0f;

    this->roll_speed = 0;
    this->azimuthf = 0.0f;
    this->elevationf = 0.0f;
    this->elevation_speedf = 0.0f;
    this->azimuth_speedf = 0.0f;
    this->control_stick_x = 0;
    this->control_stick_y = 0;
    this->ptw.Clear();
    this->incremental.Clear();
    this->smoke_set = new SCSmokeSet();
    this->smoke_set->init();
}

void SCPlane::Simulate() {
    int itemp;
    float elevtemp = 0.0f;

    uint32_t current_time = SDL_GetTicks();
    uint32_t elapsed_time = (current_time - this->last_time) / 1000;
    int newtps = 0;
    if (elapsed_time > 1) {
        uint32_t ticks = this->tick_counter - this->last_tick;
        newtps = ticks / elapsed_time;
        this->last_time = current_time;
        this->last_tick = this->tick_counter;
        if (newtps > this->tps / 2) {    
            this->tps = newtps;
        }
    }
    this->fps_knots = this->tps * (3600.0f / 6082.0f);

    this->computeGravity();
    this->processInput();
    this->updatePosition();
    this->updateSpeedOfSound();
    this->checkStatus();
    this->computeLift();
    this->computeThrust();
    this->computeDrag();
    this->updateForces();
    this->updateAcceleration();
    this->updateVelocity();
    this->updatePlaneStatus();
    
    if (this->thrust > 0) {
        itemp = this->thrust;
    } else {
        itemp = -this->thrust;
    }
    if (this->tick_counter % (100 * TPS) == 1) {
        this->fuel_rate = fuel_consump(this->Mthrust, this->W);
        this->fuel -= (int)(itemp * this->fuel_rate);
        this->inverse_mass = G_ACC / (this->W + this->fuel / 12800.0f * this->fuel_weight);
    }
    if (this->wheels) {
        this->wheel_anim --;
        if (this->wheel_anim == 0) {
            this->wheel_index ++;
            if (this->wheel_index > 5) {
                this->wheel_index = 5;
            }
            this->wheel_anim = 10;
        }
    } else {
        if (this->wheel_index>=1) {
            this->wheel_anim --;
            if (this->wheel_anim == 0) {
                this->wheel_index --;
                if (this->wheel_index < 1) {
                    this->wheel_index = 0;
                }
                this->wheel_anim = 10;
            }
        } else {
            this->wheel_index = 0;
        }
        
    }
    for (auto sim_obj: this->weaps_object) {
        sim_obj->Simulate(this->tps);
    }
    // remove dead objects
    this->weaps_object.erase(std::remove_if(this->weaps_object.begin(), this->weaps_object.end(), [](SCSimulatedObject *obj) {
        return obj->alive == false;
    }), this->weaps_object.end());
    this->object->entity->position.x = this->x;
    this->object->entity->position.y = this->y;
    this->object->entity->position.z = this->z;
    if (this->object->alive == false) {
        
        this->smoke_positions.insert(this->smoke_positions.begin(), {this->x, this->y, this->z});
        if (this->smoke_anim_counters.size() < this->smoke_set->smoke_textures.size() + 10) {
            this->smoke_anim_counters.insert(this->smoke_anim_counters.begin(), 0);
        }
        if (this->smoke_positions.size() > this->smoke_set->smoke_textures.size() + 10) {
            this->smoke_positions.pop_back();
        }
    }
    this->tick_counter++;
    this->azimuthf = this->yaw;
    this->elevationf = this->pitch;
    this->twist = this->roll;
}
void SCPlane::updatePlaneStatus() {
    this->airspeed = -(int)(this->fps_knots * this->vz);
    this->climbspeed = (short)(this->tps * (this->y - this->last_py));
    this->g_load = (this->lift_force*this->inverse_mass) / this->gravity;
    this->ax = this->acceleration.x;
    this->ay = this->acceleration.y;
    this->az = this->acceleration.z;
}

void SCPlane::updatePosition() {
    float temp{0.0f};
    
    this->ptw.Identity();
    this->ptw.translateM(this->x, this->y, this->z);

    this->ptw.rotateM(tenthOfDegreeToRad(this->yaw), 0, 1, 0);
    this->ptw.rotateM(tenthOfDegreeToRad(this->pitch), 1, 0, 0);
    this->ptw.rotateM(tenthOfDegreeToRad(this->roll), 0, 0, 1);
    
    this->ptw.translateM(this->vx/3.2808399f, this->vy/3.2808399f, this->vz/3.2808399f);
    if (round(this->yaw_speed) != 0)
        this->ptw.rotateM(tenthOfDegreeToRad(roundf(this->yaw_speed)), 0, 1, 0);
    if (round(this->pitch_speed) != 0)
        this->ptw.rotateM(tenthOfDegreeToRad(roundf(this->pitch_speed)), 1, 0, 0);
    if (round(this->roll_speed) != 0)
        this->ptw.rotateM(tenthOfDegreeToRad((float)this->roll_speed), 0, 0, 1);

    temp = 0.0f;
    this->m_old_pitch = this->pitch;
    this->pitch = (-asinf(this->ptw.v[2][1]) * 180.0f / (float)M_PI) * 10;
    this->m_pitch_var = this->m_old_pitch - this->pitch;

    float ascos = 0.0f;

    temp = cosf(tenthOfDegreeToRad(this->pitch));

    if (temp != 0.0) {

        float sincosas = this->ptw.v[2][0] / temp;

        if (sincosas > 1) {
            sincosas = 1.0f;
        } else if (sincosas < -1) {
            sincosas = -1;
        }
        this->m_old_yaw = this->yaw;
        this->yaw = asinf(sincosas) / (float)M_PI * 1800.0f;
        if (this->ptw.v[2][2] < 0.0) {
            /* if heading into z	*/
            this->yaw = 1800 - this->yaw;
        }
        if (this->yaw < 0) {
            /* then adjust		*/
            this->yaw += 3600;
        }
        this->m_yaw_var = this->m_old_yaw - this->yaw;
        float a = this->ptw.v[0][1] / temp;
        if (a > 1.0f) {
            a = 1.0f;
        } else if (a < -1.0f) {
            a = -1.0f;
        }
        this->roll = (asinf(a) / (float)M_PI) * 1800.0f;
        if (this->ptw.v[1][1] < 0.0) {
            /* if upside down	*/
            this->roll = 1800.0f - this->roll;
        }
        if (this->roll < 0) {
            this->roll += 3600.0f;
        }
    }
    /* save last position	*/
    this->last_px = this->x;
    this->last_py = this->y;
    this->last_pz = this->z;
    this->x = this->ptw.v[3][0];
    this->y = this->ptw.v[3][1];
    this->z = this->ptw.v[3][2];

    this->groundlevel = this->area->getY(this->x, this->z);
    
    this->incremental.Identity();
    if (this->roll_speed)
        this->incremental.rotateM(tenthOfDegreeToRad((float)-this->roll_speed), 0, 0, 1);
    if (this->pitch_speed)
        this->incremental.rotateM(tenthOfDegreeToRad(-this->pitch_speed), 1, 0, 0);
    if (this->yaw_speed)
        this->incremental.rotateM(tenthOfDegreeToRad(-this->yaw_speed), 0, 1, 0);
    this->incremental.translateM(this->vx, this->vy, this->vz);

    this->vx = this->incremental.v[3][0];
    this->vy = this->incremental.v[3][1];
    this->vz = this->incremental.v[3][2];

    this->forward = {
        -this->ptw.v[2][0],
        -this->ptw.v[2][1],
        -this->ptw.v[2][2],
    };
    this->position = {
        this->x,
        this->y,
        this->z
    };
}
void SCPlane::processInput() {
    int itemp {0};
    float temp {0.0f};
    float elevtemp{0.0f};
    int DELAY = this->tps/4;
    float DELAYF = this->tps/4.0f;

    /* tenths of degrees per tick	*/
    this->rollers = (this->ROLLF * ((this->control_stick_x + 8) >> 4));
    /* delta */
    itemp = (int)(this->rollers * this->vz - this->roll_speed);
    if (itemp != 0) {
        if (itemp >= DELAY || itemp <= -DELAY) {
            itemp /= DELAY;
        } else {
            itemp = itemp > 0 ? 1 : -1;
        }
    }
    if (this->wing_stall > 0) {
        itemp >>= this->wing_stall;
        itemp += mrandom(this->wing_stall << 3);
    }
    this->roll_speed += itemp;
    this->elevator = -1.0f * (this->ELEVF * ((this->control_stick_y + 8) >> 4));
    itemp = (int)(this->elevator * this->vz + this->vy - this->pitch_speed);
    elevtemp = this->elevator * this->vz + this->vy - this->pitch_speed;
    if (itemp != 0) {
        if (itemp >= DELAY || itemp <= -DELAY) {
            itemp /= DELAY;
        } else {
            itemp = itemp > 0 ? 1 : -1;
        }
    }
    if (this->wing_stall > 0) {
        itemp >>= this->wing_stall;
        elevtemp = elevtemp / powf(2, this->wing_stall);
        itemp += mrandom(this->wing_stall * 2);
        elevtemp += mrandom(this->wing_stall * 2);
    }
    this->pitch_speed += itemp;

    float max_turnrate = 0.0f;
    max_turnrate = 600.0f / tps;
    temp = this->rudder * this->vz - (4.0f) * this->vx;
    if (this->on_ground) {
        itemp = (int)(16.0f * temp);
        if (itemp < -max_turnrate || itemp > max_turnrate) {
            /* clip turn rate	*/
            if (itemp < 0) {
                itemp = -(int) max_turnrate;
            } else {
                itemp = (int) max_turnrate;
            }
            /* decrease with velocity */
            if (fabs(this->vz) > 10.0f / this->tps) {
                /* skid effect */
                temp = 0.4f * this->tps * (this->rudder * this->vz - .75f);
                if (temp < -max_turnrate || temp > max_turnrate) {
                    temp = (float)itemp;
                }
                itemp -= (int)temp;
            }
        }
        temp = (float)itemp;
    } else {
        /* itemp is desired azimuth speed	*/
        itemp = (int)temp;
    }

    itemp -= (int)this->yaw_speed;
    if (itemp != 0) {
        if (itemp >= DELAY || itemp <= -DELAY) {
            itemp /= DELAY;
        } else {
            itemp = itemp > 0 ? 1 : -1;
        }
    }
    
    this->yaw_speed += itemp;

    if (this->on_ground) {
        /* dont allow negative pitch unless positive elevation	*/
        if (this->pitch_speed < 0) {
            if (this->pitch <= 0) {
                this->pitch_speed = 0;
            }
        } else {
            /* mimic gravitational torque	*/
            elevtemp = -(this->vz * this->tps + this->MIN_LIFT_SPEED) / 4.0f;
            if (elevtemp < 0 && this->pitch <= 0) {
                elevtemp = 0.0f;
            }
            if (this->pitch_speed > elevtemp) {
                this->pitch_speed = elevtemp;
            }
        }
        this->roll_speed = 0;
    }
    this->elevation_speedf = this->pitch_speed;
    this->azimuth_speedf = this->yaw_speed;
}
void SCPlane::updateSpeedOfSound() {
    int itemp {0};
    /* compute speed of sound	*/
    if (this->y <= 36000.0f) {
        this->sos = -1116.0f / this->tps + (1116.0f - 968.0f) / this->tps / 36000.0f * this->y;
    } else {
        this->sos = -968.0f / this->tps;
    }
    itemp = ((int)this->y) >> 10;
    if (itemp > 74) {
        itemp = 74;
    } else if (itemp < 0) {
        itemp = 0;
    }
    this->ro2 = .5f * ro[itemp];
    if (this->Cl < .2) {
        this->mcc = .7166666f + .1666667f * this->Cl;
    } else {
        this->mcc = .7833333f - .1666667f * this->Cl;
    }
    /* and current mach number	*/
    this->mach = this->vz / this->sos;
    this->mratio = this->mach / this->mcc;
}
void SCPlane::checkStatus() {
    
    /****************************************************************/
    /*	check altitude for too high, and landing/takeoff            */
    /****************************************************************/
    /* flame out		*/
    if (this->y > 50000.0)
        this->thrust = 0;
    else if (this->y > this->groundlevel + 4.0) {
        /* not on ground	*/
        if (!this->takeoff) {
            this->takeoff = true;
            this->landed = false;
        }
        if (this->on_ground) {
            this->Cdp /= 3.0;
            this->min_throttle = 0;
        }
        this->on_ground = FALSE;
    } else if ((int) this->y <= this->groundlevel+2) {
        /* check for on the ground */
        if (this->object->alive == 0) {
            this->status = MEXPLODE;
        }
        
        if (this->isOnRunWay()) {
            /* and not on ground before */
            if (!this->on_ground) {
                int rating;
                /* increase drag	*/
                this->Cdp *= 3.0;
                /* allow reverse engines*/
                this->min_throttle = -this->max_throttle;
                rating = report_card(-this->climbspeed, this->roll, (int)(this->vx * this->tps),
                                        (int)(-this->vz * this->fps_knots), this->wheels);
                /* oops - you crashed	*/
                if (this->nocrash) {
                    if (rating == -1) {
                        /* set to exploding	*/
                        this->status = MEXPLODE;
                    } else {
                        this->fuel += rating << 7;
                        if (this->fuel > (100 << 7))
                            this->fuel = 100 << 7;
                        this->max_throttle = 100;
                    }
                }
            } else {
                if (this->nocrash == 0) {
                    this->status = MEXPLODE;
                }
            }
        }
        //this->ptw.v[3][1] = this->y = groundlevel;
        this->on_ground = TRUE;
        if (this->airspeed < 30 && this->thrust < 20) {
            this->thrust = 0;
            this->vx = 0.0f;
            this->vy = 0.0f;
            this->vz = 0.0f;
            this->airspeed = 0;
            if (this->takeoff) {
                this->landed = true;
            }
        }
        if (this->status > MEXPLODE) {
            /* kill negative elevation */
            if (this->pitch < 0) {
                this->pitch = 0;
            }
            /* kill any roll	*/
            if (this->roll != 0) {
                this->roll = 0;
            }
        }
    }
    if (this->fuel <= 0) {
        this->thrust = 0;
        this->max_throttle = 0;
        this->min_throttle = 0;
    }
}
void SCPlane::computeLift() {
    int itemp {0};
    this->Lmax = this->LmaxDEF * this->gravity;
    this->Lmin = this->LminDEF * this->gravity;

    this->max_cl = 1.5f + this->flaps / 62.5f;
    this->min_cl = this->flaps / 62.5f - 1.5f;
    this->tilt_factor = .005f * this->flaps + .017f;

    this->Spdf = .0025f * this->spoilers;
    this->Splf = 1.0f - .005f * this->spoilers;
    float ground_level = this->area->getY(this->x, this->z);
    if (this->y > this->gefy+ground_level) {
        // ground effect factor
        this->kl = 1.0f;
    } else {
        this->kl = ((this->y-ground_level) / this->b);
        if (this->kl > .225f) {
            this->kl = .484f * this->kl + .661f;
        } else {
            this->kl = 2.533f * this->kl + .20f;
        }
    }

    /* compute new accelerations, lift: only if vz is negative	*/
    if (this->vz < 0.0f) {
        this->ae = this->vy / this->vz + this->tilt_factor;
        this->Cl = this->uCl = this->ae / (.17f + this->kl * this->ipi_AR);
        /* check for positive stall	*/
        if (this->Cl > this->max_cl) {
            this->Cl = 3.0f * this->max_cl - 2.0f * this->Cl;
            this->wing_stall = 1;
            if (this->Cl < 0.0f) {
                this->wing_stall += 1 - (int)(this->Cl / this->max_cl);
                this->Cl = 0.0f;
            }
            if (this->uCl > 5.0f) {
                this->uCl = 5.0f;
            }
        } else if (this->Cl < this->min_cl) {
            /* check for negative stall	*/
            this->Cl = 3.0f * this->min_cl - 2.0f * this->Cl;
            this->wing_stall = 1;
            if (this->Cl > 0.0f) {
                this->wing_stall += 1 - (int)(this->Cl / this->min_cl);
                this->Cl = 0.0f;
            }
            if (this->uCl < -5.0f) {
                this->uCl = -5.0f;
            }
        } else {
            this->wing_stall = FALSE;
        }
    } else {
        this->Cl = this->uCl = 0.0f;
        this->wing_stall = this->on_ground ? 0 : (int)this->vz;
        this->ae = 0.0f;
    }
    if (this->wing_stall > 64) {
        this->wing_stall = 64;
    }

    /* assume V approx = vz	*/
    this->qs = this->ro2 * this->vz * this->s;

    this->lift = this->Cl * this->qs;
    this->g_limit = FALSE;
    if (this->spoilers > 0) {
        this->lift *= this->Splf;
    }
    this->lift_drag_force = -this->vy * this->lift;
    this->lift_force = this->vz * this->lift;
    /* save for wing loading meter	*/
    this->lift = this->lift_force * this->inverse_mass;
    if (this->vz == 0.0f) {
        /* if no vertical speed, then no lift	*/
        this->lift = 0.0f;
        this->lift_drag_force = 0.0f;
        this->lift_force = 0.0f;
    }
    while (this->lift > this->Lmax || this->lift < this->Lmin) {
        /* if lift is out of bounds, adjust it	*/
        if (this->lift > this->Lmax) {
            this->lift = .99f * this->Lmax / this->inverse_mass / this->vz;
        } else if (this->lift < this->Lmin) {
            this->lift = .99f * this->Lmin / this->inverse_mass / this->vz;
        }
        this->g_limit = TRUE;
        this->lift_drag_force = -this->vy * this->lift;
        this->lift_force = this->vz * this->lift;
        this->lift = this->lift_force * this->inverse_mass;
    }
}
void SCPlane::computeDrag() {
    if (this->mratio < 1.034f) {
        this->Cdc = 0.0f;
    } else {
        this->Cdc = .082f * this->mratio - 0.084788f;
        if (this->Cdc > .03f)
            this->Cdc = .03f;
    }
    if (this->spoilers > 0.0f) {
        this->Cdc += this->Spdf;
    }
    this->Cd = this->Cdp + this->kl * this->uCl * this->uCl * this->ie_pi_AR + this->Cdc;
    this->drag = this->Cd * this->qs;
    this->gravity_drag_force = this->vy * this->drag ;
    this->drag_force = this->vz * this->drag ;
}
void SCPlane::computeGravity() {
    this->gravity = G_ACC / this->tps / this->tps;
    this->gravity_force = this->gravity * this->W;
}
void SCPlane::computeThrust() {
    this->thrust_force = .01f / this->tps / this->tps * this->thrust * this->Mthrust * this->engine_power_fraction;

}
void SCPlane::updateAcceleration() {
    
    this->acceleration.y *= this->inverse_mass;
    this->acceleration.z *= this->inverse_mass;

    this->acceleration.x -= this->ptw.v[0][1] * this->gravity;
    this->acceleration.y -= this->ptw.v[1][1] * this->gravity;
    this->acceleration.z -= this->ptw.v[2][1] * this->gravity;
}
void SCPlane::updateForces() {
    int vz_sign = (this->vz < 0.0f) ? 1 : -1;
    this->acceleration.x = 0.0f;
    this->acceleration.y = this->lift_force + (vz_sign * this->gravity_drag_force);
    this->acceleration.z = this->lift_drag_force - this->thrust_force + (vz_sign * this->drag_force);
}
void SCPlane::updateVelocity() {
    float temp{0.0f};
    this->vx += this->acceleration.x;
    this->vz += this->acceleration.z;
    if (this->on_ground && this->status > MEXPLODE) {
        temp = 0.0f;
        float mcos;
        this->vx = 0.0;
        gl_sincos(this->pitch, &temp, &mcos);
        temp = this->vz * temp / mcos;
        if (this->vy + this->acceleration.y < temp) {
            this->acceleration.y = temp - this->vy;
        }
    }
    this->vy += this->acceleration.y;
}
/**
 * IN_BOX: Check if the plane is inside a box.
 * 
 * @param llx Lower left X of the box.
 * @param urx Upper right X of the box.
 * @param llz Lower left Z of the box.
 * @param urz Upper right Z of the box.
 * @return 1 if the plane is inside, 0 otherwise.
 */
int SCPlane::IN_BOX(int llx, int urx, int llz, int urz) {
    return (llx <= this->x && this->x <= urx && urz <= this->z && this->z <= llz );
}
/**
 * Check if the plane is currently on a runway.
 *
 * @return 1 if the plane is on a runway, 0 otherwise.
 */
int SCPlane::isOnRunWay() {
    // area is legitimately null for space missions (no runway/ground —
    // see SCJdynPlane::Simulate's groundlevel comment). Normally the "on
    // ground" branch that calls this never triggers there (groundlevel
    // defaults to a huge negative sentinel), but afterburner's much larger
    // vz feeds into lift/drag formulas that were never recalibrated for
    // that range, which can send y to an extreme value and trip it anyway
    // — confirmed by a live crash (SIGSEGV dereferencing null area here).
    if (area == nullptr) {
        return 0;
    }
    for (int i = 0; i < area->objectOverlay.size(); i++) {

        if (IN_BOX(area->objectOverlay[i].lx, area->objectOverlay[i].hx, -area->objectOverlay[i].ly,
                   -area->objectOverlay[i].hy)) {

            return 1;
        }
    }
    return 0;
}
float SCPlane::fuel_consump(float f, float b) { return 0.3f * f / b; }
/**
 * report_card: Evaluate the landing quality of the plane.
 *
 * @param descent_rate Vertical speed of the plane at landing.
 * @param roll_angle Angle of roll at landing.
 * @param velocity_x Horizontal speed of the plane at landing.
 * @param velocity_z Vertical speed of the plane at landing.
 * @param wheels_down Is the landing gear down?
 *
 * @return A score indicating the quality of the landing:
 *         1: Good
 *         0: Fair
 *        -1: Bad
 */
int SCPlane::report_card(int descent_rate, float roll_angle, int velocity_x, int velocity_z, int wheels_down) {
    int on_runway = isOnRunWay();
    int rating = 1;

    roll_angle /= 10;
    if (roll_angle > 180)
        roll_angle -= 360;

    if (!wheels_down)
        rating = 0;
    if (descent_rate > 100)
        rating = 0;
    if (roll_angle < 0)
        roll_angle = -roll_angle;
    if (roll_angle > 10)
        rating = 0;
    if (!on_runway)
        rating = 0;
    else if (velocity_x > 10 || velocity_x < -10)
        rating = 0;

    if (roll_angle > 20 || descent_rate > 20 || velocity_x > 20 || velocity_x < -20)
        rating = -1;

    return rating;
}

void SCPlane::SetThrottle(int throttle) { this->thrust = throttle; }
int SCPlane::GetThrottle() { return this->thrust; }
void SCPlane::SetFlaps() {
    if (this->flaps == this->Fmax) {
        this->flaps = 0;
    } else {
        this->flaps = (int) this->Fmax;
    }
}
void SCPlane::SetSpoilers() {
    if (this->spoilers == this->Smax) {
        this->spoilers = 0;
    } else {
        this->spoilers = (int) this->Smax;
    }
}
void SCPlane::SetWheel() { this->wheels = !this->wheels; }
short SCPlane::GetWheel() { return this->wheels; }
int SCPlane::GetFlaps() { return this->flaps; }
int SCPlane::GetSpoilers() { return this->spoilers; }
void SCPlane::SetControlStick(int x, int y) {
    this->control_stick_x = x;
    this->control_stick_y = y;
}
void SCPlane::getPosition(Point3D *position) {
    position->x = this->x;
    position->y = this->y;
    position->z = this->z;
}

/**
 * Renders the plane object at its current position and orientation.
 *
 * If the wheels are down, renders the wheels at the correct position and
 * animates them. If the thrust is greater than 50, renders the jet engine
 * at the correct position and scales it according to the thrust level.
 *
 * If the plane is carrying any weapons, renders them at the correct position
 * 
 */
void SCPlane::Render() {
    
    if (this->object != nullptr) {
        
        Vector3D pos = {
            this->x, this->y, this->z
        };
        Vector3D orientation = {
            tenthOfDegreeToRad(this->yaw + 900),
            tenthOfDegreeToRad(this->pitch),
            -tenthOfDegreeToRad(this->roll)
        };
        std::vector<std::tuple<Vector3D, RSEntity*>> weapons;
        for (auto weaps:this->weaps_load) {
            float decy=0.5f;
            if (weaps == nullptr) {
                continue;  
            }
            if (weaps->hpts_type == 0) {
                continue;
            }
            if (weaps->hpts_type == 4) {
                decy = 0.0f;
            }
            Vector3D position = weaps->position;
            std::vector<Vector3D> path = {
                {0, -2*decy, 0},
                {decy, -decy, 0},
                {-decy,-decy,0},
                {0, -2*decy, -2*decy},
                {decy, -decy, -2*decy},
                {-decy,-decy,-2*decy}
            };

            for (int i = 0; i < weaps->nb_weap; i++) {
                if (i >= path.size()) {
                    break;
                }
                Vector3D weap_pos = {position.z/250+path[i].z, position.y/250 + path[i].y, -position.x/250+path[i].x};
                std::tuple<Vector3D, RSEntity*> weapon = std::make_tuple(weap_pos, weaps->objct);
                weapons.push_back(weapon);
                position.y -= 0.5f;
            }
        }
        Renderer.drawModelWithChilds(this->object->entity, Renderer.lodLevel, pos, orientation, wheel_index, thrust, weapons, this->afterburner_engaged);
    }
}
void SCPlane::RenderSmoke() {
    int cpt = 0;
    const int MAX_SMOKE=7;
    std::map<int, int> anim_map = {
        {0, 0}, {1, 1}, {2, 3}, {3, 2}, {4, 4}, {5, 2}, {6, 4}
    };
    

    // Ensure we don't divide by zero
    int frames_per_tick = this->tps / 12;
    if (frames_per_tick < 1) frames_per_tick = 1;
    this->anim_ticks++;
    int anim_frame = 0;
    int nb_smoke = (int) this->smoke_positions.size();
    for (auto pos: this->smoke_positions) {
        int id_anim = 4;
        if (cpt < 5) {
            cpt++;
            continue;
        }
        
        int id_map = (cpt - 5);
        if (id_map >= anim_map.size()-1) {
            id_map = (int) (anim_map.size()-1)-(cpt % 2);
        }
        id_anim = anim_map[id_map];
        int frame = 0;
        if (id_anim < 0 || id_anim >= this->smoke_set->smoke_textures.size()) {
            continue;
        }
        if (this->smoke_set->smoke_textures[id_anim].size() > 0) {
            if (this->anim_ticks % frames_per_tick == 0) {
                this->smoke_anim_counters[cpt] = (this->smoke_anim_counters[cpt]+1) % (this->smoke_set->smoke_textures[id_anim].size());
            }
            float alpha = ((nb_smoke-5) - (cpt-5)) / ((float) nb_smoke-5.0f);
            Texture *tex = this->smoke_set->smoke_textures[id_anim][this->smoke_anim_counters[cpt]];
            if (tex != nullptr) {
                Renderer.drawBillboard(pos, tex, 10, alpha);
            }
        }
        cpt++;
    }
}
void SCPlane::RenderSimulatedObject() {
    for (auto sim_obj: this->weaps_object) {
        sim_obj->Render();
    }
}
Vector3D SCPlane::getWeaponIntialVector(float speedFactor) {
    Vector3D initial_trust = {0,0,0};
    Vector3D planeVelocity       = {
        (this->x - this->last_px) * this->tps,
        (this->y - this->last_py) * this->tps,
        (this->z - this->last_pz) * this->tps
    };
    return planeVelocity + this->forward * speedFactor;

}
// Lazily initializes (first call) and regenerates this->gun_energy_current
// against wall-clock time, returning the up-to-date value -- shared by
// Shoot()'s own energy-gun case and SCStrike's FIRE_PRIMARY handler (which
// needs a current reading *before* calling Shoot() to decide whether to fire
// every matching hardpoint together or alternate one at a time when energy's
// too low for a full salvo). Kept as the single source of truth for this
// calc rather than duplicating it at both call sites.
float SCPlane::GetCurrentGunEnergy() {
    RSEntity *shipEntity = this->object->entity;
    uint32_t nowTicks = SDL_GetTicks();
    if (this->gun_energy_current < 0.0f) {
        this->gun_energy_current = (float)shipEntity->gun_energy_capacity;
        this->gun_energy_last_update_ticks = nowTicks;
    }
    float elapsedSec = (float)(nowTicks - this->gun_energy_last_update_ticks) / 1000.0f;
    // /5.0f: RSEntity::gun_energy_recharge_rate's own comment -- confirmed
    // against live gameplay (HELLCAT/HELLCATP's raw values only match the
    // observed ~10-11s empty-to-full recharge time once divided by 5).
    float regenerated = this->gun_energy_current
        + (float)shipEntity->gun_energy_recharge_rate / 5.0f * this->gun_power_fraction * elapsedSec;
    float capacity = (float)shipEntity->gun_energy_capacity;
    this->gun_energy_current = (regenerated < capacity) ? regenerated : capacity;
    this->gun_energy_last_update_ticks = nowTicks;
    return this->gun_energy_current;
}
bool SCPlane::IsWC3EnergyGunWeaponId(int weaponId) {
    return weaponId == ID_NEUTGUN || weaponId == ID_IONGUN || weaponId == ID_RLASER_WC3 ||
        weaponId == ID_REAPGUN || weaponId == ID_TACHGUN;
}
// Same as the single-argument overload above, but fires along a direction
// pitched pitchOffsetDegrees up from this->forward (toward the ship's own
// up vector) instead of straight down the boresight -- used to converge gun
// hardpoint fire closer to the HUD crosshair. Computes "up" the same way
// setCameraFollow/setCameraBelow do, but from yaw/pitch/roll (not twist) to
// stay in the same basis this->forward itself was derived from
// (SCPlane::updatePosition's ptw, built from yaw/pitch/roll).
Vector3D SCPlane::getWeaponIntialVector(float speedFactor, float pitchOffsetDegrees) {
    Vector3D planeVelocity = {
        (this->x - this->last_px) * this->tps,
        (this->y - this->last_py) * this->tps,
        (this->z - this->last_pz) * this->tps
    };
    float r_azim = tenthOfDegreeToRad(this->yaw);
    float r_elev = tenthOfDegreeToRad(this->pitch);
    float r_roll = tenthOfDegreeToRad(this->roll);
    float cosR = cosf(r_roll), sinR = sinf(r_roll);
    float cosE = cosf(r_elev), sinE = sinf(r_elev);
    float cosA = cosf(r_azim), sinA = sinf(r_azim);
    Vector3D up = {
        -cosA * sinR + sinA * sinE * cosR,
        cosE * cosR,
        sinA * sinR + cosA * sinE * cosR
    };
    float theta = pitchOffsetDegrees * ((float)M_PI / 180.0f);
    Vector3D adjustedForward = this->forward * cosf(theta) + up * sinf(theta);
    return planeVelocity + adjustedForward * speedFactor;
}
bool SCPlane::Shoot(int weapon_hard_point_id, SCMissionActors *target, SCMission *mission, float lockProgressSeconds, float spawnAdvanceDistance) {
    if (weapon_hard_point_id < 0 || (size_t)weapon_hard_point_id >= this->weaps_load.size()) {
        return false;
    }
    SCWeaponLoadoutHardPoint *weap_loadout{nullptr};
    weap_loadout = this->weaps_load[weapon_hard_point_id];
    if (weap_loadout == nullptr) {
        return false;
    }
    // WC3 torpedo/T-bomb (weapon_category==3): refuse to fire at all below
    // the weapon's own required lock time — per user direction. Read
    // directly off wdat->lock_time_required_seconds, real data parsed off
    // the weapon's own OBJT>MISL>DATA chunk (parseREAL_OBJT_MISL_DATA) —
    // confirmed exact against two real missile data points (see that
    // function's own comment); torpedo/T-bomb specifically use the same
    // parser/field (both are OBJT>MISL format too) but haven't had their
    // own decoded values externally cross-checked yet. AI callers pass
    // the 999.0f lockProgressSeconds default (they gate firing via their
    // own fire-arc/range checks instead — see SCMissionActors.cpp), so
    // this only ever actually blocks the player.
    if (weap_loadout->objct->wdat != nullptr && weap_loadout->objct->wdat->weapon_category == 3) {
        if (lockProgressSeconds < weap_loadout->objct->wdat->lock_time_required_seconds) {
            return false;
        }
    }
    if (this->pilot != nullptr && this->pilot->actor_name != "PLAYER") {
        int precision = std::rand() % 16;
        if (precision <= this->pilot->profile->ai.atrb.AA || weap_loadout->objct->wdat->weapon_id == ID_MK20 || weap_loadout->objct->wdat->weapon_id == ID_MK82) {
            return this->ShootWithPrediction(weapon_hard_point_id, target, mission, lockProgressSeconds);
        }
    }
    SCSimulatedObject *weap{nullptr};
    Vector3D initial_trust = {0,0,0};

    MemSound *sound;
    if (this->wp_cooldown > 0) {
        this->wp_cooldown--;
        return false;
    }
    RSEntity *wobj = nullptr;
    wobj = this->weaps_load[weapon_hard_point_id]->objct;
    switch (wobj->wdat->weapon_id) {
        case ID_20MM:
            weap = new GunSimulatedObject();
            initial_trust = this->getWeaponIntialVector(1000.0f); // coefficient ajustable
            this->wp_cooldown = 3; // Cooldown between two shots
        break;
        case ID_NEUTGUN:
        case ID_IONGUN:
        case ID_RLASER_WC3:
        case ID_REAPGUN:
        case ID_TACHGUN: {
            // Energy-capacitor gated (RSEntity::gun_energy_capacity/
            // _recharge_rate on the SHIP's own entity, not the weapon's
            // synthetic one) instead of ammo-count limited — see
            // gun_energy_current's own comment (SCPlane.h) for the lazy
            // on-demand regen model.
            this->GetCurrentGunEnergy();
            // Real per-shot energy cost, parsed off the weapon's own
            // OBJT>GUNS>DATA chunk (RSEntity::parseREAL_OBJT_GUNS_DATA) —
            // confirmed exact against real Tachyon Gun stats. Falls back
            // to a small nonzero placeholder only if wdat is somehow
            // still a default-constructed WDAT (e.g. asset failed to
            // resolve — see getWC3RealWeaponEntity's own printf for that
            // case).
            float energyCost = (wobj->wdat->energy_cost > 0) ? (float)wobj->wdat->energy_cost : 5.0f;
            if (this->gun_energy_current < energyCost) {
                return false;
            }
            this->gun_energy_current -= energyCost;
            weap = new GunSimulatedObject();
            // +5 degrees pitched up from the ship's own boresight, to
            // converge gun fire closer to the HUD crosshair -- per user
            // direction, not a value derived from any game data.
            initial_trust = this->getWeaponIntialVector(1000.0f, 5.0f);
            // Real refire delay (seconds) converted to ticks at the plane's
            // own current tick rate, parsed off OBJT>GUNS>DATA — confirmed
            // exact against real Tachyon Gun stats (89/256s ~= 0.35s).
            // Falls back to the old flat 3-tick placeholder if unset.
            this->wp_cooldown = (wobj->wdat->refire_delay_seconds > 0.0f)
                ? (int)(wobj->wdat->refire_delay_seconds * (float)this->tps)
                : 3;
        break;
        }
        case ID_HSMISS:
        case ID_IRMISS:
        case ID_FFMISS:
            // Missiles fired without a lock go straight ahead (dumbfire) —
            // per user direction. Mirrors ID_LAU3's own unguided branch
            // exactly (guidance=false, no target, orientation from the
            // plane's own current heading below) rather than reimplementing
            // it. Locked missiles get the existing guided pursuit-steer
            // behavior, identical to SC1's own missiles (the `default`
            // case below).
            //
            // Required lock time is per-weapon, read directly off wdat->
            // lock_time_required_seconds (real, parsed off the weapon's own
            // OBJT>MISL>DATA — see parseREAL_OBJT_MISL_DATA's own comment),
            // not a single shared constant — confirmed real data shows this
            // genuinely varies per missile: Friend-or-Foe requires 0s (locks
            // instantly, effectively always guided whenever a target is
            // passed in at all), Image-Recognition requires a real 1s hold.
            weap = new SCSimulatedObject();
            initial_trust = this->getWeaponIntialVector(1.0f);
            initial_trust.Scale(1.0f/(float)tps);
            // target != nullptr guard: FFMISS's 0s lock_time_required_seconds
            // means lockProgressSeconds(0) >= required(0) is true even with
            // nothing targeted (SCStrike's FIRE_MISSILE handler now allows
            // firing these three IDs with target==nullptr for dumbfire) —
            // without this, guidance would end up true with a null target,
            // which SCSimulatedObject::Simulate()'s pursuit-steer code
            // dereferences.
            if (target != nullptr && lockProgressSeconds >= wobj->wdat->lock_time_required_seconds) {
                weap->guidance = true;
                weap->target = target;
            } else {
                weap->guidance = false;
            }
            this->wp_cooldown = 10;
        break;
        case ID_MINEMISS:
            // Drop and stay put — no initial_trust (stays {0,0,0}), and
            // SCSimulatedObject::ComputeTrajectory holds it stationary
            // every tick from here on (see WDAT::is_guided_flight). No
            // target: the existing dumbfire proximity-scan-every-actor
            // path in Simulate() is what actually detonates it once an
            // enemy strays close.
            weap = new SCSimulatedObject();
            weap->guidance = false;
            this->wp_cooldown = 10;
        break;
        case ID_TORKMISS:
        case ID_TEMBMISS:
            // Reaching here means the lock_time_required gate at the top
            // of this function already passed — always guided, same as a
            // locked missile.
            weap = new SCSimulatedObject();
            initial_trust = this->getWeaponIntialVector(1.0f);
            initial_trust.Scale(1.0f/(float)tps);
            weap->guidance = true;
            weap->target = target;
            this->wp_cooldown = 10;
        break;
        case ID_MK20:
        case ID_MK82:
        case ID_DURANDAL:
            initial_trust = this->getWeaponIntialVector(1.0f);
            weap = new GunSimulatedObject();
            if (this->pilot->mission->sound.sounds.size() > 0) {
                sound = this->pilot->mission->sound.sounds[SoundEffectIds::MK82_DROP];
                Mixer.playSoundVoc(sound->data, sound->size);
            }
            this->wp_cooldown = 10; // Cooldown between two shots
        break;
        case ID_LAU3:
            weap = new SCSimulatedObject();
            initial_trust = this->getWeaponIntialVector(1.0f);
            initial_trust.Scale(1.0f/(float)tps);
            if (this->pilot->mission->sound.sounds.size() > 0) {
                sound = this->pilot->mission->sound.sounds[SoundEffectIds::POD_SHOOT];
                Mixer.playSoundVoc(sound->data, sound->size);
            }
            weap->guidance = false;
            weap->no_gravity = false;
            wobj = wobj->weaps[0]->objct;
            this->wp_cooldown = 10;
        break;
        case ID_GBU15:
            weap = new SCSimulatedObject();
            initial_trust = this->getWeaponIntialVector(1.0f);
            initial_trust.Scale(1.0f/(float)tps);
            if (this->pilot->mission->sound.sounds.size() > 0) {
                sound = this->pilot->mission->sound.sounds[SoundEffectIds::MK82_DROP];
                Mixer.playSoundVoc(sound->data, sound->size);
            }
            weap->guidance = true;
            weap->no_gravity = false;
            weap->target = target;
            this->wp_cooldown = 10;
        break;
        default:
            initial_trust = this->getWeaponIntialVector(1.0f);
            initial_trust.Scale(1.0f/(float)tps);
            if (this->pilot->mission->sound.sounds.size() > 0) {
                sound = this->pilot->mission->sound.sounds[SoundEffectIds::AIM9_SHOOT];
                Mixer.playSoundVoc(sound->data, sound->size);
            }
            weap = new SCSimulatedObject();
            weap->target = target;
            this->wp_cooldown = 10; // Cooldown between two shots
        break;
    }
    weap->mission = mission;
    weap->shooter = this->pilot;
    
    if (this->weaps_load[weapon_hard_point_id]->nb_weap <= 0) {
        weapon_hard_point_id = (int) this->object->entity->hpts.size()-weapon_hard_point_id;
        if (weapon_hard_point_id<=0) {
            return false;
        }
        if (weapon_hard_point_id > this->weaps_load.size()-1) {
            return false;
        }
        if (this->weaps_load[weapon_hard_point_id] == nullptr) {
            return false;
        }
        if (this->weaps_load[weapon_hard_point_id]->nb_weap == 0) {
            return false;
        }
    }
    // WC3 energy guns (NEUTGUN/ION_GUN/RLASER/REAPGUN/TACHGUN) set nb_weap=1
    // as a plain "hardpoint occupied" sentinel, not a real ammo count -- see
    // RSEntity::parseREAL_OBJT_SSHP_WEAP_FGTR_GUNS's own comment. They're
    // gated purely by gun_energy_current/energy_cost above, so decrementing
    // nb_weap here would hit 0 after the very first shot and make every
    // subsequent Shoot() call on that hardpoint fail the nb_weap<=0 check
    // near the top of this function as if ammo had run out.
    // Bug found (2026-07 session, user-reported: weapons MFD missile count
    // showing "-664"): the objct->weaps[0] branch below reads/decrements
    // this->weaps_load[hpid]->objct->weaps[0]->nb_weap -- but objct is the
    // weapon-TYPE's own shared RSEntity (the same prototype object every
    // ship mounting that weapon type points to), and its ->weaps[0] here is
    // whatever RSEntity::parseREAL_OBJT_PODR_DATA populated from that
    // weapon's own OBJT>PODR>DATA chunk -- a submunition/pod count, nothing
    // to do with this hardpoint's remaining ammo. For missiles/torpedoes
    // that chunk is unrelated and the counter is shared across every ship
    // (including AI) firing that same weapon type, so gating the real
    // ammo decrement behind it let the displayed count drift to nonsense
    // values. The real per-hardpoint ammo count is already correctly
    // tracked in this->weaps_load[hpid]->nb_weap itself (seeded from the
    // ship's own SSHP>WEAP>FGTR>MISL ammo field -- see SCPlane::InitLoadout),
    // so only guns -- where this dual-counter path may have been intended --
    // still take it; every non-gun weapon now always decrements its own
    // hardpoint's real count directly, one per shot.
    if (!this->infinite_ammo && !IsWC3EnergyGunWeaponId(wobj->wdat->weapon_id)) {
        bool isGunCategory = wobj->wdat->weapon_category == 0;
        if (isGunCategory && this->weaps_load[weapon_hard_point_id]->objct->weaps.size() > 0) {
            this->weaps_load[weapon_hard_point_id]->objct->weaps[0]->nb_weap--;
            if (this->weaps_load[weapon_hard_point_id]->objct->weaps[0]->nb_weap <= 0) {
                this->weaps_load[weapon_hard_point_id]->nb_weap--;
            }
        } else {
            this->weaps_load[weapon_hard_point_id]->nb_weap--;
        }
    }
    weap->obj = wobj;

    // Spawn from the fired hardpoint's actual mount position (ship-local
    // offset, parsed off SSHP>WEAP>FGTR>GUNS -- see RSEntity::parseREAL_
    // OBJT_SSHP_WEAP_FGTR_GUNS / SCPlane::InitLoadout()) instead of the
    // ship's center, by rotating it into world space with a fresh
    // yaw/pitch/roll rotation. Deliberately not this->ptw: that member still
    // carries this frame's extra velocity-translate/turn-rate sub-step by
    // the time Shoot() runs (see SCPlane::updatePosition()), so reusing it
    // here would apply that per-frame delta twice.
    Matrix hardpointRotation;
    hardpointRotation.Clear();
    hardpointRotation.Identity();
    hardpointRotation.rotateM(tenthOfDegreeToRad(this->yaw), 0, 1, 0);
    hardpointRotation.rotateM(tenthOfDegreeToRad(this->pitch), 1, 0, 0);
    hardpointRotation.rotateM(tenthOfDegreeToRad(this->roll), 0, 0, 1);
    Vector3DHomogeneous hardpointLocal;
    hardpointLocal.x = weap_loadout->position.x;
    hardpointLocal.y = weap_loadout->position.y;
    hardpointLocal.z = weap_loadout->position.z;
    hardpointLocal.w = 0.0f;
    Vector3DHomogeneous hardpointWorld = hardpointRotation.multiplyMatrixVector(hardpointLocal);

    weap->x = this->x + hardpointWorld.x;
    weap->y = this->y + hardpointWorld.y;
    weap->z = this->z + hardpointWorld.z;
    weap->azimuthf = this->yaw;
    weap->elevationf = this->pitch;
    weap->vx = initial_trust.x;
    weap->vy = initial_trust.y;
    weap->vz = initial_trust.z;
    if (spawnAdvanceDistance != 0.0f) {
        Vector3D velocityDir = {weap->vx, weap->vy, weap->vz};
        velocityDir.Normalize();
        weap->x += velocityDir.x * spawnAdvanceDistance;
        weap->y += velocityDir.y * spawnAdvanceDistance;
        weap->z += velocityDir.z * spawnAdvanceDistance;
    }

    weap->weight = wobj->weight_in_kg*2.205f;

    // Torgo 2 (MISNJ002.IFF) mine-count-per-nav-point win condition (user-
    // described, 2026-07 session): matched to the nearest JUBOUY buoy actor
    // at drop time, not at detonation — mines sit exactly where dropped
    // (SCSimulatedObject::ComputeTrajectory never moves them again once
    // spawned), so the drop position IS where the player judged the nav
    // point to be. See OP_GET_MINE_COUNT_AT_NAVPOINT (228, SCProg.cpp) for
    // the read side. Player-only: AI never carries/fires mines in practice,
    // and the mechanic is specifically a player action per the mission
    // objective text ("Mine jump point"). kMineDeployRadius is an estimate
    // (no real data pins an exact "close enough" distance for this
    // mechanic) — smaller than OP_76's confirmed 15000-unit "start
    // engaging" radius, since dropping a mine implies deliberate precise
    // placement, not just being in the general vicinity.
    if (wobj->wdat->weapon_id == ID_MINEMISS && mission != nullptr &&
        this->pilot != nullptr && this->pilot->actor_name == "PLAYER") {
        static const float kMineDeployRadius = 3000.0f;
        SCMissionActors *nearestBuoy = nullptr;
        float nearestDist = 0.0f;
        Vector3D dropPos = {weap->x, weap->y, weap->z};
        for (auto candidate : mission->actors) {
            if (candidate == nullptr || candidate->object == nullptr) {
                continue;
            }
            if (candidate->object->member_name != "JUBOUY") {
                continue;
            }
            float dist = (candidate->object->position - dropPos).Length();
            if (nearestBuoy == nullptr || dist < nearestDist) {
                nearestBuoy = candidate;
                nearestDist = dist;
            }
        }
        if (nearestBuoy != nullptr && nearestDist <= kMineDeployRadius) {
            nearestBuoy->mines_deployed++;
        }
    }

    this->weaps_object.push_back(weap);
    return true;
}
void SCPlane::InitLoadout() {
    // this->object->entity->weaps
    // this->object->entity->hpts
    this->weaps_load.resize(this->object->entity->hpts.size());
    for (int i=0; i < this->weaps_load.size(); i++) {
        this->weaps_load[i] = nullptr;
    }
    // WC3 (SSHP) ships: each hardpoint has exactly one fixed weapon baked
    // into the file, already parsed 1:1 by index into this->weaps
    // (RSEntity::parseREAL_OBJT_SSHP_WEAP_FGTR_GUNS/MISL — this->weaps[i]
    // is always the weapon mounted at this->hpts[i]). This is a
    // fundamentally different shape than SC1's JETP loadout (a weapon
    // *type*+count list matched against hardpoint *slots* separately by
    // the weap_map/max_load_out tables below, since JETP loadouts are
    // swappable and WC3's aren't) — the matching/symmetric-splitting
    // algorithm below doesn't apply and would mis-assign weapons if run
    // against WC3 data. Detected via weaps.size()==hpts.size(): JETP's
    // own weaps list is sized to the ship's weapon *type* count, which is
    // essentially never equal to its hardpoint *slot* count.
    auto &entityWeaps = this->object->entity->weaps;
    auto &entityHpts = this->object->entity->hpts;
    if (!entityWeaps.empty() && entityWeaps.size() == entityHpts.size()) {
        this->is_wc3_ship = true;
        int armedCount = 0;
        for (size_t i = 0; i < entityHpts.size(); i++) {
            if (entityWeaps[i]->objct == nullptr) {
                continue;
            }
            SCWeaponLoadoutHardPoint *weap = new SCWeaponLoadoutHardPoint();
            weap->objct = entityWeaps[i]->objct;
            weap->nb_weap = entityWeaps[i]->nb_weap;
            weap->hpts_type = entityHpts[i]->id;
            weap->name = entityWeaps[i]->name;
            weap->position = {
                (float) entityHpts[i]->x,
                (float) entityHpts[i]->y,
                (float) entityHpts[i]->z
            };
            weap->hud_pos = {0, 0};
            this->weaps_load[i] = weap;
            armedCount++;
        }
        printf("SCPlane::InitLoadout: WC3 branch, %d/%zu hardpoints armed (unarmed ones failed weapon-id resolution -- see RSEntity's own printf)\n",
               armedCount, entityHpts.size());
        return;
    }
    printf("SCPlane::InitLoadout: SC1 (JETP) branch -- entity->weaps.size()=%zu, entity->hpts.size()=%zu\n",
           entityWeaps.size(), entityHpts.size());
    std::unordered_map<int, std::vector<int>> weap_map = {
        {ID_20MM, {0}},
        {ID_AIM9J, {4, 1, 2}},
        {ID_AIM9M, {4, 1, 2}},
        {ID_AGM65D, {2,3}},
        {ID_LAU3, {2,3}},
        {ID_MK20, {2,3}},
        {ID_MK82, {2,3}},
        {ID_DURANDAL, {2,3}},
        {ID_GBU15, {2,3}},
        {ID_AIM120, {1,2}},
    };
    std::unordered_map<int, std::unordered_map<int, int>> max_load_out = {
        {ID_20MM, {{0, 1000}}},
        {ID_AIM9J, {{4, 1}, {1, 1}, {2, 2}}},
        {ID_AIM9M, {{4, 1}, {1, 1}, {2, 2}}},
        {ID_AGM65D, {{2, 3}, {3, 3}}},
        {ID_LAU3, {{2, 3}, {3, 2}}},
        {ID_MK20, {{2, 3}, {3, 3}}},
        {ID_MK82, {{2, 6}, {3, 6}}},
        {ID_DURANDAL, {{2, 3}, {3, 3}}},
        {ID_GBU15, {{2, 1}, {3, 1}}},
        {ID_AIM120, {{1, 1}, {2, 2}}}
    };
    for (auto loadout: this->object->entity->weaps) {
        if (loadout->nb_weap == 0) {
            continue;
        }
        if (loadout->objct->wdat == nullptr) {
            continue;
        }
        int nbweap = loadout->nb_weap;
        while (nbweap > 0) {
            // affectation des armes sur les différents points durs
            int hpt_id = -1;
            for (int i=0; i < this->object->entity->hpts.size() && hpt_id == -1; i++) {
                for (auto hpt_type: weap_map[loadout->objct->wdat->weapon_id]) {
                    if (this->object->entity->hpts[i]->id == hpt_type && this->weaps_load[i] == nullptr) {
                        hpt_id = i;
                        break;
                    }
                }
            }
            if (hpt_id == -1) {
                nbweap = 0;
                continue;
            }
            SCWeaponLoadoutHardPoint *weap = new SCWeaponLoadoutHardPoint();
            weap = new SCWeaponLoadoutHardPoint();
            weap->objct = loadout->objct;
            weap->nb_weap = loadout->nb_weap;
            weap->hpts_type = this->object->entity->hpts[hpt_id]->id;
            weap->name = loadout->name;
            weap->position = {
                (float) this->object->entity->hpts[hpt_id]->x,
                (float) this->object->entity->hpts[hpt_id]->y,
                (float) this->object->entity->hpts[hpt_id]->z
            };
            weap->hud_pos = {0, 0};
            this->weaps_load[hpt_id] = weap;
            // affectation symétrique
            if (hpt_id == 0) {
                nbweap -= weap->nb_weap;
                continue;
            }
            // set the max loadout for this weapon and hard point type
            int maxloadout = max_load_out[loadout->objct->wdat->weapon_id][weap->hpts_type];
            weap->nb_weap = nbweap / 2;
            if (weap->nb_weap > maxloadout) {
                weap->nb_weap = maxloadout;
            }
            
            nbweap -= weap->nb_weap;
            int second_hpt_id = (int)this->object->entity->hpts.size()-hpt_id;
            if (second_hpt_id != hpt_id && second_hpt_id < this->weaps_load.size()) {
                weap = new SCWeaponLoadoutHardPoint();
                weap->objct = loadout->objct;
                weap->nb_weap = loadout->nb_weap / 2;
                weap->hpts_type = this->object->entity->hpts[second_hpt_id]->id;
                weap->name = loadout->name;
                weap->position = {
                    (float) this->object->entity->hpts[second_hpt_id]->x,
                    (float) this->object->entity->hpts[second_hpt_id]->y,
                    (float) this->object->entity->hpts[second_hpt_id]->z
                };
                weap->hud_pos = {0, 0};
                this->weaps_load[second_hpt_id] = weap;
            }
            // fin affectation symétrique
            //
            maxloadout = max_load_out[loadout->objct->wdat->weapon_id][weap->hpts_type];
            if (weap->nb_weap > maxloadout) {
                weap->nb_weap = maxloadout;
            }
            nbweap -= weap->nb_weap;
        }
    }
}
void SCPlane::renderPlaneLined() {
    if (this->object != nullptr) {
        
        Vector3D pos = {
            this->x, this->y, this->z
        };
        Vector3D orientation = {
            this->yaw/10.0f + 90,
            this->pitch/10.0f,
            -this->roll/10.0f
        };
        std::vector<std::tuple<Vector3D, RSEntity*>> weapons;
        for (auto weaps:this->weaps_load) {
            float decy=0.5f;
            if (weaps == nullptr) {
                continue;  
            }
            if (weaps->hpts_type == 0) {
                continue;
            }
            if (weaps->hpts_type == 4) {
                decy = 0.0f;
            }
            Vector3D position = weaps->position;
            std::vector<Vector3D> path = {
                {0, -2*decy, 0},
                {decy, -decy, 0},
                {-decy,-decy,0},
                {0, -2*decy, -2*decy},
                {decy, -decy, -2*decy},
                {-decy,-decy,-2*decy}
            };

            for (int i = 0; i < weaps->nb_weap; i++) {
                Vector3D weap_pos = {position.z/250+path[i].z, position.y/250 + path[i].y, -position.x/250+path[i].x};
                std::tuple<Vector3D, RSEntity*> weapon = std::make_tuple(weap_pos, weaps->objct);
                weapons.push_back(weapon);
                position.y -= 0.5f;
            }
        }
        //Renderer.drawModelWithChilds(this->object->entity, LOD_LEVEL_MAX, pos, orientation, wheel_index, thrust, weapons);
        
        BoudingBox *bb = this->object->entity->GetBoudingBpx();
        Vector3D position = {this->x, this->y, this->z};          
        
        int vert_id = 0;
        int wing_id_left = 0;
        std::vector<int> wing_ids;
        for (auto vertex: this->object->entity->vertices) {
            if (vertex.x == bb->min.x) {
                Renderer.drawPoint(vertex, {1.0f,0.0f,0.0f}, position, orientation);
            }
            if (vertex.x == bb->max.x) {
                Renderer.drawPoint(vertex, {0.0f,1.0f,0.0f}, position, orientation);
            }
            if (vertex.z == bb->min.z) {
                wing_ids.push_back(vert_id);
                Renderer.drawPoint(vertex, {1.0f,0.0f,0.0f}, position, orientation);
            }
            if (vertex.z == bb->max.z) {
                wing_ids.push_back(vert_id);
                Renderer.drawPoint(vertex, {0.0f,1.0f,0.0f}, position, orientation);
            }
            vert_id++;
        }
        
        std::vector<int> wing_tr_id;
        std::vector<Point2Df> wing_surface_points;
        for (int i=0; i < this->object->entity->lods[LOD_LEVEL_MAX].numTriangles; i++) {
            int triangle_id = this->object->entity->lods[LOD_LEVEL_MAX].triangleIDs[i];
            if (triangle_id < 0 || triangle_id >= this->object->entity->triangles.size()) {
                continue; // Skip invalid triangle IDs
            }
            Triangle triangle = this->object->entity->triangles[triangle_id];
            
            for (auto id : triangle.ids) {
                if (std::find(wing_ids.begin(), wing_ids.end(), id) != wing_ids.end()) {
                    Vector3D v0,v1,v2;
                    v0 = this->object->entity->vertices[triangle.ids[0]];
                    v1 = this->object->entity->vertices[triangle.ids[1]];
                    v2 = this->object->entity->vertices[triangle.ids[2]];

                    Point2Df p0 = {v0.x, v0.z};
                    if (std::find(wing_surface_points.begin(), wing_surface_points.end(), p0) == wing_surface_points.end()) {
                        wing_surface_points.push_back(p0);
                    }

                    Point2Df p1 = {v1.x, v1.z};
                    if (std::find(wing_surface_points.begin(), wing_surface_points.end(), p1) == wing_surface_points.end()) {
                        wing_surface_points.push_back(p1);
                    }
                    Point2Df p2 = {v2.x, v2.z};
                    if (std::find(wing_surface_points.begin(), wing_surface_points.end(), p2) == wing_surface_points.end()) {
                        wing_surface_points.push_back(p2);
                    }
                    if (std::find(wing_tr_id.begin(), wing_tr_id.end(), triangle_id) == wing_tr_id.end()) {
                        wing_tr_id.push_back(triangle_id);
                    }
                    v0  = this->object->entity->vertices[triangle.ids[0]];
                    v1  = this->object->entity->vertices[triangle.ids[1]];
                    v2  = this->object->entity->vertices[triangle.ids[2]];

                    Renderer.drawPoint(v0, {0.0f,1.0f,1.0f}, position, orientation);
                    Renderer.drawPoint(v1, {0.0f,1.0f,1.0f}, position, orientation);
                    Renderer.drawPoint(v2, {0.0f,1.0f,1.0f}, position, orientation);
                }
            }
        }

        for (int i=0; i < this->object->entity->lods[LOD_LEVEL_MAX].numTriangles; i++) {
            int triangle_id = this->object->entity->lods[LOD_LEVEL_MAX].triangleIDs[i];
            if (triangle_id < 0 || triangle_id >= this->object->entity->triangles.size()) {
                continue; // Skip invalid triangle IDs
            }
            Vector3D v0,v1,v2;
            v0  = this->object->entity->vertices[this->object->entity->triangles[triangle_id].ids[0]];
            v1  = this->object->entity->vertices[this->object->entity->triangles[triangle_id].ids[1]];
            v2  = this->object->entity->vertices[this->object->entity->triangles[triangle_id].ids[2]];
            if (std::find(wing_tr_id.begin(), wing_tr_id.end(), triangle_id) != wing_tr_id.end()) {
                Renderer.drawLine(v0,v1,{1.0f,0.0f,0.0f},orientation, position);
                Renderer.drawLine(v1,v2,{1.0f,0.0f,0.0f},orientation, position);
                Renderer.drawLine(v2,v0,{1.0f,0.0f,0.0f},orientation, position);
            } else {
                Renderer.drawLine(v0,v1,{0.5f,0.5f,0.5f},orientation, position);
                Renderer.drawLine(v1,v2,{0.5f,0.5f,0.5f},orientation, position);
                Renderer.drawLine(v2,v0,{0.5f,0.5f,0.5f},orientation, position);
            }
        }
        if (!wing_surface_points.empty()) {
            float area = 0.0f;
            
            // Compute the convex hull of wing_surface_points using the monotone chain algorithm.
            auto cross = [](const Point2Df &O, const Point2Df &A, const Point2Df &B) -> float {
                return (A.x - O.x) * (B.y - O.y) - (A.y - O.y) * (B.x - O.x);
            };

            // Tri des points par coordonnées (x, puis y)
            std::sort(wing_surface_points.begin(), wing_surface_points.end(), [](const Point2Df &a, const Point2Df &b) {
                return (a.x == b.x) ? (a.y < b.y) : (a.x < b.x);
            });

            std::vector<Point2Df> hull;
            // Construction de la chaîne inférieure
            for (const auto &p : wing_surface_points) {
                while (hull.size() >= 2 && cross(hull[hull.size()-2], hull.back(), p) <= 0)
                    hull.pop_back();
                hull.push_back(p);
            }
            // Construction de la chaîne supérieure
            for (int i = (int)wing_surface_points.size() - 2, t = (int) hull.size() + 1; i >= 0; i--) {
                while (hull.size() >= t && cross(hull[hull.size()-2], hull.back(), wing_surface_points[i]) <= 0)
                    hull.pop_back();
                hull.push_back(wing_surface_points[i]);
            }
            // Retirer le dernier point car il est identique au premier
            if (!hull.empty())
                hull.pop_back();
            
            
            wing_surface_points = hull;
            size_t n = wing_surface_points.size();
            // Calcul de la surface selon la formule du polygone (formule de shoelace)
            for (size_t i = 0; i < n; i++) {
                Vector3D sp1 = {wing_surface_points[i].x, 0.0f , wing_surface_points[i].y};
                Vector3D sp2 = {wing_surface_points[(i + 1) % n].x, 0.0f , wing_surface_points[(i + 1) % n].y};
                Renderer.drawLine(sp1,sp2,{1.0f,1.0f,0.0f},orientation, position);
            }
            for (size_t i = 0; i < n; i++) {
                size_t next = (i + 1) % n;
                area += wing_surface_points[i].x * wing_surface_points[next].y - wing_surface_points[i].y * wing_surface_points[next].x;
            }
            area = std::fabs(area) / 2.0f;
        }
        Vector3D scaled_forward{
            this->forward.x,
            this->forward.y,
            this->forward.z
        };
        scaled_forward.Scale(10.0f);
        Renderer.drawLine(pos, scaled_forward, {0.0f, 0.0f, 1.0f});
        
        Vector3D ptw_up = {
            this->ptw.v[1][0],
            this->ptw.v[1][1],
            this->ptw.v[1][2]
        };
        ptw_up.Normalize();
        Vector3D ptw_forward = {
            -this->ptw.v[2][0],
            -this->ptw.v[2][1],
            -this->ptw.v[2][2]
        };
        Vector3D ptw_right = {
            this->ptw.v[0][0],
            this->ptw.v[0][1],
            this->ptw.v[0][2]
        };

        ptw_forward.Normalize();
        ptw_up.Scale(10.0f);
        ptw_forward.Scale(10.0f);
        ptw_right.Scale(10.0f);
        Renderer.drawLine(pos, ptw_up, {0.0f, 1.0f, 1.0f});
        Renderer.drawLine(pos, ptw_forward, {0.0f, 1.0f, 0.0f});
        Renderer.drawLine(pos, ptw_right, {1.0f, 0.0f, 0.0f});

        /*Renderer.drawLine(pos, {
            this->acceleration.x * 10.0f,
            this->acceleration.y * 10.0f,
            this->acceleration.z * 10.0f
        }, {0.0f, 1.0f, 0.0f});*/
    }
}
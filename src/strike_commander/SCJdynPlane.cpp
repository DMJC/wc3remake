#include "precomp.h"
#include "../engine/gametimer.h"

const float GRAVITY = 9.80f; // m/s^2
const float AIR_DENSITY = 1.225f; // kg/m^3
const float DRAG_COEFFICIENT = 0.47f;
const float MAX_LIFT_COEFFICIENT = 2.0f; // Coefficient de portance maximum
const float LIFT_COEFFICIENT = 0.5f; // Doit être ajusté en fonction de la forme de l'objet
const float WING_AREA = 1.0f; // m^2
SCJdynPlane::SCJdynPlane() {

}
SCJdynPlane::SCJdynPlane(float LmaxDEF, float LminDEF, float Fmax, float Smax, float ELEVF_CSTE, float ROLLFF_CSTE, float s, float W,
        float fuel_weight, float Mthrust, float b, float ie_pi_AR, int MIN_LIFT_SPEED,
        RSArea *area, float x, float y, float z
    ): SCPlane(LmaxDEF, LminDEF, Fmax, Smax, ELEVF_CSTE, ROLLFF_CSTE, s, W, fuel_weight, Mthrust, b, ie_pi_AR, MIN_LIFT_SPEED, area, x, y, z) {
    this->acceleration = {0.0f, 0.0f, 0.0f};
    this->velocity = {0.0f, 0.0f, 0.0f};
    this->thrust_force = 0.0f;
    this->lift_force = 0.0f;
    this->vx = 0.0f;
    this->vy = 0.0f;
    this->vz = 0.0f;
    this->ax = 0.0f;
    this->ay = 0.0f;
    this->az = 0.0f;
    this->weaps_load.reserve(9);
    this->weaps_load.resize(9);
    this->alive = 0;
    this->status = 0;
    this->x = 0.0f;
    this->y = 0.0f;
    this->z = 0.0f;
    this->roll = 0;
    this->roll_speed = 0;
    this->yaw = 0.0f;
    this->pitch = 0.0f;
    this->pitch_speed = 0.0f;
    this->yaw_speed = 0.0f;
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
    this->fuel_weight = 0;
    this->fuel = (int) fuel_weight;
    this->fuel_max = fuel_weight;
    this->Mthrust = Mthrust;
    this->b = b;
    
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
    this->Cdp = .015f;
    this->ro2 = 0.5f * (AIR_DENSITY - 0.000112f * this->y / 1000.0f); // Approximation atmosphère standard;
    this->ipi_AR = 1.0f / ((float)M_PI * this->b * this->b / this->s);
    this->ie_pi_AR = 0.83f * this->ipi_AR;
    this->wheels = 1;
    this->on_ground = 1;
    this->status = 580000;
    this->alive = 1;
    this->tilt_factor = 0.17f;
    this->inverse_mass = 1.0f / (this->W + (this->fuel  * 0.81f));
    this->ptw.Clear();
    this->incremental.Clear();
}
SCJdynPlane::~SCJdynPlane() {

}
void SCJdynPlane::Simulate() {
    int itemp;
    float elevtemp = 0.0f;
    float dt = GameTimer::getInstance().getDeltaTime();
    if (dt <= 0.0f) {
        dt = 0.016f; // Approx 60 FPS
    }
    float ftps = (1.0f / dt);
    if (ftps < 1.0f) {
        ftps = 1.0f;
    }
    this->tps = (uint32_t)(ftps + 0.5f);

    this->gravity = GRAVITY * dt * dt;
    this->fps_knots = 1.944f / dt;
    // Space missions have no terrain (area stays null — see
    // WC3Mission::loadMission()). Pin groundlevel far below any reachable
    // altitude so checkStatus()'s "not on ground" branch always wins,
    // instead of dereferencing a null area.
    this->groundlevel = this->area ? this->area->getY(this->x, this->z) : -1000000.0f;
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

    // vz is now a real per-second rate (KPS, see computeThrust/updatePosition),
    // not a per-frame delta, so it no longer needs dividing by dt to recover
    // a rate here — that used to undo the old per-frame-delta convention.
    float vitesse_ms = abs(this->vz);
    this->airspeed = (int)(vitesse_ms * 1.944f);
    this->climbspeed = (short)(dt / (this->y - this->last_py));
    this->g_load = (this->lift_force*this->inverse_mass) / this->gravity;
    this->ax = this->acceleration.x*10.0f;
    this->ay = this->acceleration.y*10.0f;
    this->az = this->acceleration.z;

    if (this->thrust > 0) {
        itemp = this->thrust;
    } else {
        itemp = -this->thrust;
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

    // Check for NaN values in velocity components
    if (std::isnan(this->vx) || std::isnan(this->vy) || std::isnan(this->vz)) {
        // Log or output debug information
        printf("NaN velocity detected! Resetting velocity components.\n");
        
        // Reset velocity to prevent simulation instability
        this->vx = 0.0f;
        this->vy = 0.0f;
        this->vz = 0.0f;
        
        // Reset related values that might be affected
        this->airspeed = 0;
        this->acceleration.x = 0.0f;
        this->acceleration.y = 0.0f;
        this->acceleration.z = 0.0f;
    }

    // Also check for NaN in rotation speeds
    if (std::isnan(this->roll_speed) || std::isnan(this->pitch_speed) || std::isnan(this->yaw_speed)) {
        printf("NaN rotation speed detected! Resetting rotation components.\n");
        
        // Reset rotation speeds
        this->roll_speed = 0;
        this->pitch_speed = 0.0f;
        this->yaw_speed = 0.0f;
    }
    // Check for NaN in position and rotation angles
    if (std::isnan(this->yaw) || std::isnan(this->pitch) || std::isnan(this->roll)) {
        printf("NaN detected in rotation angles! Resetting angles.\n");
        
        // Reset angles to prevent further instability
        if (std::isnan(this->yaw)) {
            this->yaw = 0.0f;
            this->yaw_speed = 0.0f;
        }
        
        if (std::isnan(this->pitch)) {
            this->pitch = 0.0f;
            this->pitch_speed = 0.0f;
        }
        
        if (std::isnan(this->roll)) {
            this->roll = 0.0f;
            this->roll_speed = 0;
        }
        
        // Also update the derived values
        this->azimuthf = this->yaw;
        this->elevationf = this->pitch;
        this->twist = this->roll;
    }
    
}

void SCJdynPlane::updatePosition() {
    float temp{0.0f};
    float dt = GameTimer::getInstance().getDeltaTime();

    // Integrate this frame's incremental rotation into the persistent
    // orientationQuat (gimbal-lock-free) instead of rebuilding "current
    // orientation" from yaw/pitch/roll (re-extracted from ptw every frame
    // below) and re-applying it via 3 Euler rotateM calls — that compose/
    // re-extract round-trip is what causes gimbal lock as pitch nears
    // +-90 (asin-based extraction becomes singular there), confirmed by
    // live testing once the new dps-calibrated turn rates made reaching
    // that range trivial. yaw/pitch/roll below are still computed each
    // frame (from the resulting ptw) for every other system that reads
    // them (camera, HUD, AI, ...), but orientationQuat itself is never
    // reconstructed from them, so that feedback loop is broken — this
    // integration step (dq_yaw/dq_pitch/dq_roll composed onto the
    // existing orientationQuat below) is the ship's only real source of
    // truth for its orientation.
    Quaternion dq_yaw, dq_pitch, dq_roll;
    dq_yaw.FromAxisAngle(0, 1, 0, tenthOfDegreeToRad(this->yaw_speed));
    dq_pitch.FromAxisAngle(1, 0, 0, tenthOfDegreeToRad(this->pitch_speed));
    dq_roll.FromAxisAngle(0, 0, 1, tenthOfDegreeToRad((float)this->roll_speed));
    // All three are applied body-relative via PRE-multiply (dq on the
    // left, dq.Multiply(&orientationQuat) then orientationQuat = dq) —
    // yawing, pitching, and rolling should always turn the ship around its
    // OWN current nose/wing/up axes (its center), never around a fixed
    // world axis, regardless of current attitude.
    //
    // This looks backwards from the "usual" quaternion rule (post-multiply
    // = body-relative), and an earlier version of this code trusted that
    // usual rule and used post-multiply for pitch/roll — but it's wrong
    // for THIS codebase's specific combination of row-vector matrices
    // (Vector3D::transformPoint does p*M, not M*p) and FromAxisAngle's own
    // negated half-angle (chosen to match Matrix::rotateM's rotation
    // direction, see its own comment). Verified numerically (simulating
    // Quaternion::Multiply/FromAxisAngle/ToMatrix exactly): starting from
    // a 90-degree-yawed orientation and post-multiplying a "pitch"
    // increment rotates around the fixed world X axis regardless of the
    // yaw already applied (forward vector doesn't move); pre-multiplying
    // the same increment correctly rotates around the ship's current
    // right axis instead. Each increment below is composed against the
    // most up to date orientationQuat (yaw's result feeds pitch's
    // pre-multiply, pitch's feeds roll's), so a later axis in the chain
    // still turns around the ship's true current attitude including this
    // same frame's earlier increments. No gimbal lock either way (pure
    // quaternion composition, no Euler round trip).
    dq_yaw.Multiply(&this->orientationQuat);
    this->orientationQuat = dq_yaw;
    dq_pitch.Multiply(&this->orientationQuat);
    this->orientationQuat = dq_pitch;
    dq_roll.Multiply(&this->orientationQuat);
    this->orientationQuat = dq_roll;
    this->orientationQuat.Normalize();
    Matrix orientationMatrix = this->orientationQuat.ToMatrix();

    this->ptw.Identity();
    this->ptw.translateM(this->x, this->y, this->z);
    this->ptw.Multiply(&orientationMatrix);

    // vz is now a real KPS rate (computeThrust's rate-limited ramp), unlike
    // vx/vy which stay on the older small-per-frame-delta convention (still
    // driven by the force/inverse_mass pipeline, untouched here) — so only
    // vz needs an explicit *dt to become "world units this frame" instead
    // of moving a full KPS-worth of distance every single frame. Assumes
    // 1 KPS == 1 world unit/second (per live-testing feedback).
    this->ptw.translateM(this->vx, this->vy, this->vz * dt);

    temp = 0.0f;
    this->m_old_pitch = this->pitch;
    this->pitch = (-asinf(this->ptw.v[2][1]) * 180.0f / (float)M_PI) * 10;
    this->m_pitch_var = this->m_old_pitch - this->pitch;
    
    temp = cosf(tenthOfDegreeToRad(this->pitch));

    if (temp != 0.0f) {

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

    float rad_roll_speed = tenthOfDegreeToRad((float)this->roll_speed);
    float rad_pitch_speed = tenthOfDegreeToRad(this->pitch_speed);
    float rad_yaw_speed = tenthOfDegreeToRad(this->yaw_speed);

    // vx/vy/vz are body-frame velocity (see computeLift/computeDrag/
    // updateAcceleration treating vz as forward speed and projecting world
    // gravity into body space via ptw's own rows) — the ptw composition
    // above (translateM(vx,vy,vz*dt) applied through the CURRENT
    // orientationMatrix) already reprojects them into world space fresh
    // every frame using the ship's true current attitude, so they need no
    // separate "carry the turn forward" step.
    //
    // This used to additionally spin vx/vy/vz here via a from-scratch,
    // world-fixed-axis Matrix::rotateM sequence (this->incremental,
    // reset to Identity every frame — copied from the older SCPlane/
    // SCSimplePlane base implementation, where it made sense: THAT
    // updatePosition() rebuilds ptw's whole orientation from absolute
    // yaw/pitch/roll via the same fixed-axis rotateM every frame, so
    // incremental's rotation at least spoke the same language as ptw's
    // own). Once orientation moved to the persistent, gimbal-lock-free
    // orientationQuat above, this block became a second, INCONSISTENT
    // rotation of the same vector — never chained against the real
    // orientation, so it drifted further from the ship's true attitude
    // every frame it turned, injecting spurious body-frame velocity that
    // the position integration above then applied as real translation.
    // That's what sent the ship swinging away from where its nose was
    // actually pointing while pitching/yawing (reported: pitching swings
    // the view through the TCS Victory's flight deck near the mission's
    // spawn point) instead of turning in place around itself.

    float deltaTime = 1.0f / this->tps;
    

    // Filtre passe-bas : alpha contrôle la réactivité vs lissage
    // alpha = 0.1 -> très lissé, alpha = 0.5 -> moins lissé
    const float alpha = 0.1f;
    
    Vector3D raw_angular_velocity = {
        -rad_pitch_speed,
        -rad_yaw_speed,
        -rad_roll_speed
    };

    this->angular_velocity.x = alpha * raw_angular_velocity.x + (1.0f - alpha) * this->angular_velocity.x;
    this->angular_velocity.y = alpha * raw_angular_velocity.y + (1.0f - alpha) * this->angular_velocity.y;
    this->angular_velocity.z = alpha * raw_angular_velocity.z + (1.0f - alpha) * this->angular_velocity.z;
    
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
void SCJdynPlane::processInput() {
    float dt = GameTimer::getInstance().getDeltaTime();

    // WC3 ships (RSEntity::parseREAL_OBJT_SSHP_DYNM_FGTR) carry their own
    // calibrated max pitch/yaw/roll dps stats. "WC3 doesn't have real
    // physics" (live-tested ground truth): turn rate is a fixed dps value
    // independent of airspeed, not a control-surface-authority-times-
    // velocity model — so bypass the vz-scaled formula below entirely for
    // these ships and drive roll/pitch/yaw_speed (tenths of a degree this
    // frame — see updatePosition's rotateM calls) directly from stick
    // input and the ship's own dps stat. ROLL_RATE/TWIST_RATE are
    // misleadingly named (inherited from this struct's SC1/JETP shape) —
    // manual cross-check confirms ROLL_RATE is actually pitch dps and
    // TWIST_RATE is actually yaw dps; WC3_TRUE_ROLL_RATE_DPS is the real
    // roll dps, previously parsed and discarded.
    if (this->object != nullptr && this->object->entity != nullptr && this->object->entity->jdyn != nullptr &&
        this->object->entity->jdyn->WC3_TRUE_ROLL_RATE_DPS > 0) {
        JDYN *dyn = this->object->entity->jdyn;
        constexpr float kStickFullDeflection = 200.0f;
        // Raw JDYN dps values (e.g. Hellcat's 60/60/60) taken as literal
        // degrees/second work out to a 6.0s full 360 at max deflection —
        // sluggish next to WC3's remembered snappy arcade turning, and
        // unlike computeThrust's spool-up time this dps interpretation was
        // never verified against live DOS timing. Doubled per live-testing
        // feedback (still felt slow at full stick/Shift-held deflection,
        // i.e. this isn't the separate half/full-rate keyboard scheme
        // below in checkKeyboard) rather than re-deriving an exact
        // ground-truth multiplier from DOSBox.
        constexpr float kWC3TurnRateMultiplier = 2.0f;
        auto axisSpeedTenths = [&](float stickValue, float maxDps) -> float {
            float ratio = stickValue / kStickFullDeflection;
            if (ratio > 1.0f) ratio = 1.0f;
            if (ratio < -1.0f) ratio = -1.0f;
            return ratio * maxDps * kWC3TurnRateMultiplier * 10.0f * dt;
        };
        // rudder's own natural range is +-10 (SCStrike::checkKeyboard ramps
        // it by +-0.1/frame), not the +-200-ish stick range, so rescale it
        // onto the same kStickFullDeflection basis before normalizing.
        constexpr float kRudderFullDeflection = 10.0f;
        // Negated: Quaternion::FromAxisAngle's rotation-direction fix (see
        // its own comment — needed to match Matrix::rotateM's convention
        // and resolve the cockpit/orientation desync) flipped the visual
        // effect of a positive pitch/roll_speed for every axis uniformly.
        // Yaw stayed correct because it's driven through rudder's own
        // separate ramp, not control_stick_x/y directly, but pitch/roll's
        // sign (tuned against the pre-fix convention) needed re-flipping —
        // confirmed by live testing.
        this->pitch_speed = axisSpeedTenths(-this->control_stick_y, (float)dyn->ROLL_RATE);
        this->roll_speed = axisSpeedTenths(-this->control_stick_x, (float)dyn->WC3_TRUE_ROLL_RATE_DPS);
        this->yaw_speed = axisSpeedTenths(this->rudder * (kStickFullDeflection / kRudderFullDeflection), (float)dyn->TWIST_RATE);
        this->elevation_speedf = this->pitch_speed;
        this->azimuth_speedf = this->yaw_speed;
        return;
    }

    int itemp {0};
    float temp {0.0f};
    float elevtemp{0.0f};
    int DELAY = (int)(0.25f / dt);  // 1/4 de seconde

    float DELAYF = dt * 0.25f;

    /* tenths of degrees per tick	*/
    this->rollers = (this->ROLLF * ((this->control_stick_x + 8) >> 4));
    /* delta */
    itemp = (int)(this->rollers * (this->vz*3.2f) - this->roll_speed);
    if (DELAY == 0) {
        DELAY = 15; // Avoid division by zero
    }
    if (itemp != 0) {
        if (itemp >= DELAY || itemp <= -DELAY) {
            itemp /= DELAY;
        } else {
            itemp = itemp > 0 ? 1 : -1;
        }
    }
    if (this->wing_stall > 0) {
        itemp >>= this->wing_stall;
        //itemp += mrandom(this->wing_stall << 3);
    }
    this->roll_speed += itemp;
    this->elevator = -1.0f * (this->ELEVF * ((this->control_stick_y + 8) >> 4));
    itemp = (int)(this->elevator * (this->vz*3.2f) + (this->vy*3.2f) - this->pitch_speed);
    elevtemp = this->elevator * (this->vz*3.2f) + (this->vy*3.2f) - this->pitch_speed;
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
        //itemp += mrandom(this->wing_stall * 2);
        //elevtemp += mrandom(this->wing_stall * 2);
    }
    this->pitch_speed += itemp;

    float max_turnrate = 0.0f;
    max_turnrate = 600.0f * dt;
    if (this->rudder != 0) {
        printf("Rudder input detected: %f\n", this->rudder);
    }
    temp = this->rudder * (this->vz*3.2f) - (4.0f) * (this->vx*3.2f);
    if (this->on_ground) {
        itemp = (int)(16.0f * temp);
        if (itemp < -max_turnrate || itemp > max_turnrate) {
            /* clip turn rate	*/
            if (itemp < 0) {
                itemp = - (int)max_turnrate;
            } else {
                itemp = (int)max_turnrate;
            }
            /* decrease with velocity */
            if (fabs(this->vz*3.2) > 10.0f *dt) {
                /* skid effect */
                temp = 0.4f / dt * (this->rudder * (this->vz*3.2f) - .75f);
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
            elevtemp = -((this->vz*3.2f) / dt + this->MIN_LIFT_SPEED) / 4.0f;
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
void SCJdynPlane::updateSpeedOfSound() {
    int itemp {0};
    float dt = GameTimer::getInstance().getDeltaTime();
    
    if (this->y <= 11000.0f) {
        this->sos = -340.3f * dt + (340.3f - 295.0f) * dt / 11000.0f * this->y;
    } else {
        this->sos = -295.0f * dt;
    }
    itemp = ((int)this->y) >> 10;
    if (itemp > 74) {
        itemp = 74;
    } else if (itemp < 0) {
        itemp = 0;
    }
    this->ro2 = 0.5f * (AIR_DENSITY - 0.000112f * this->y / 1000.0f); // Approximation atmosphère standard;
    if (this->Cl < .2) {
        this->mcc = .7166666f + .1666667f * this->Cl;
    } else {
        this->mcc = .7833333f - .1666667f * this->Cl;
    }
    /* and current mach number	*/
    if (this->sos == 0.0f) {
        this->sos = 1.0f; // Avoid division by zero
    }
    if (this->mcc == 0.0f) {
        this->mcc = 1.0f; // Avoid division by zero
    }
    this->mach = this->vz / this->sos;
    this->mratio = this->mach / this->mcc;
}
void SCJdynPlane::checkStatus() {
    float dt = GameTimer::getInstance().getDeltaTime();
    
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
            this->Cdp = 0.015f;
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
                this->Cdp = 0.085f;
                /* allow reverse engines*/
                this->min_throttle = -this->max_throttle;
                rating = report_card(-this->climbspeed, this->roll, (int)(this->vx / dt),
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
                this->Cdp = 0.085f;
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
void SCJdynPlane::computeLift() {
    int itemp {0};
    this->Lmax = this->object->entity->jdyn->MAX_G * this->gravity;
    this->Lmin = -0.5f * this->object->entity->jdyn->MAX_G * this->gravity;

    this->max_cl = 1.5f + this->flaps / 62.5f;
    this->min_cl = this->flaps / 62.5f - 1.5f;
    this->tilt_factor = .005f * this->flaps + .017f;

    this->Spdf = .0025f * this->spoilers;
    this->Splf = 1.0f - .005f * this->spoilers;
    
    if (this->y > this->gefy+this->groundlevel) {
        // ground effect factor
        this->kl = 1.0f;
    } else {
        float height_ratio = (this->y - this->groundlevel) / this->b;

        if (height_ratio > 0.15f) {
            // Transition douce vers l'altitude normale
            this->kl = 0.85f + 0.15f * (height_ratio / 0.3f);
        } else {
            // Effet de sol modéré près du sol
            this->kl = 0.7f + 1.0f * height_ratio;
        }
    }

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
    while (this->lift > this->Lmax || this->lift < this->Lmin) {
        /* if lift is out of bounds, adjust it	*/
        if (this->vz < 0.0f) {
            if (this->lift > this->Lmax) {
                this->lift = .99f * this->Lmax / this->inverse_mass / this->vz;
            } else if (this->lift < this->Lmin) {
                this->lift = .99f * this->Lmin / this->inverse_mass / this->vz;
            }
            this->g_limit = TRUE;
            this->lift_drag_force = -this->vy * this->lift;
            this->lift_force = this->vz * this->lift;
            this->lift = this->lift_force * this->inverse_mass;
        } else {
            this->lift = 0.0f;
            this->lift_drag_force = 0.0f;
            this->lift_force = 0.0f;
        }
            
    }
}
void SCJdynPlane::computeDrag() {
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
void SCJdynPlane::computeGravity() {
    float dt = GameTimer::getInstance().getDeltaTime();
    
    this->inverse_mass = 1.0f / (this->W + (this->fuel));
    this->gravity = GRAVITY *dt *dt;
    this->gravity_force = this->gravity * (this->W + (this->fuel));
}
void SCJdynPlane::computeThrust() {
    float dt = GameTimer::getInstance().getDeltaTime();
    // Mthrust is constructed from RSEntity::thrust_in_newton, which despite
    // its name is actually the ship's calibrated max normal-flight speed in
    // KPS (confirmed against the game manual's per-ship stats: e.g. Arrow
    // 520, Excalibur 500, Hellcat 420, Thunderbolt 380 all match exactly).
    // While the afterburner key is held, scale toward the ship's own
    // max-afterburner-speed stat instead — RSEntity::weight_in_kg, which
    // the same manual cross-check showed is actually that value (Arrow
    // 1400, Excalibur 1300, Hellcat 1200, Thunderbolt 1000), not mass —
    // rather than applying one hardcoded multiplier for every ship.
    float mthrust = this->Mthrust;
    float abMaxSpeed = mthrust;
    if (this->object != nullptr && this->object->entity != nullptr) {
        abMaxSpeed = (float)this->object->entity->weight_in_kg;
    }
    // WC3 "doesn't have real physics" (live DOS-original ground truth,
    // timed directly against the running game): a stationary Hellcat V
    // (420 KPS max) reaches full speed in ~2.2s and holds there — a clean
    // linear ramp with a hard cap, not a thrust/drag equilibrium. Arrow's
    // manual figures independently predict ~2.08s (520/250). One constant
    // reasonably stands in for a real per-ship "Acceleration" stat, which
    // isn't present anywhere in the parsed JDYN data.
    //
    // thrust_force here is therefore the signed vz delta needed THIS
    // FRAME to move current speed toward the throttle target, rate-limited
    // by kSpoolUpSeconds — applied directly in updateForces() instead of
    // being combined with lift_drag_force/drag_force, since (per the same
    // ground truth) aerodynamic drag doesn't fight thrust here the way it
    // would under a real force model; folding it in was why the previous
    // attempt still felt too slow despite a correctly-calibrated target.
    constexpr float kSpoolUpSeconds = 2.08f;
    // Afterburner always targets the ship's full AB speed regardless of throttle.
    // On release, the target falls back to the throttle-proportional speed and
    // the ship decelerates at the same rate as normal throttle changes.
    float targetSpeed = this->afterburner_engaged
        ? abMaxSpeed
        : mthrust * (this->thrust / 100.0f);
    float currentSpeed = -this->vz; // vz is negative for forward motion
    // Rate is always based on normal max so AB spool-up matches throttle feel.
    float maxDelta = (mthrust / kSpoolUpSeconds) * dt;
    float diff = targetSpeed - currentSpeed;
    if (diff > maxDelta) diff = maxDelta;
    if (diff < -maxDelta) diff = -maxDelta;
    // updateAcceleration() unconditionally divides acceleration.z by mass
    // right after updateForces() applies -thrust_force to it (that step
    // exists for the old F=ma force model). diff here is already the
    // exact velocity delta the ramp wants, not a force, so pre-multiply by
    // mass to cancel that division out — otherwise the ramp ends up ~1000x
    // slower than kSpoolUpSeconds, since W (mass) is itself the mislabeled
    // weight_in_kg field (~1000-1400, not a real small aircraft mass).
    this->thrust_force = diff * (this->W + this->fuel);
}
void SCJdynPlane::updateAcceleration() {
    if (this->acceleration.x != 0.0f) {
        this->acceleration.x *= this->inverse_mass;
    }
    this->acceleration.y *= this->inverse_mass;
    this->acceleration.z *= this->inverse_mass;

    // this->gravity itself stays nonzero even in space (it's also the 1G
    // reference unit for Lmax/Lmin/g_load structural-stress limits) — only
    // skip actually applying it as a directional pull.
    if (!this->no_gravity) {
        this->acceleration.x -= this->ptw.v[0][1] * this->gravity;
        this->acceleration.y -= this->ptw.v[1][1] * this->gravity;
        this->acceleration.z -= this->ptw.v[2][1] * this->gravity;
    }
}
void SCJdynPlane::updateForces() {
    int vz_sign = (this->vz < 0.0f) ? 1 : -1;
    this->acceleration.x = 0.0f;
    // Aerodynamic lift requires atmosphere — it's already meaningless for
    // space combat, and gravity itself is correctly skipped here via
    // no_gravity (see updateAcceleration), but lift_force/gravity_drag_force
    // were still being applied to acceleration.y unconditionally. That was
    // roughly harmless while vz stayed tiny (the old per-frame-delta
    // convention), but now that vz is a real KPS-scale rate (up to
    // 500-1400+), lift_force's own vz-dependent terms (some of which
    // divide by vz) blow up, sending y to an extreme value — confirmed by
    // live testing: at high speed this tripped SCJdynPlane::checkStatus's
    // "on ground" branch, which unconditionally sets on_ground=true and,
    // if airspeed/thrust happen to read low in that same degenerate frame,
    // landed=true — which SCMission::update() treats as mission complete.
    this->acceleration.y = this->no_gravity ? 0.0f : (this->lift_force + (vz_sign * this->gravity_drag_force));
    // thrust_force (computeThrust) is already the rate-limited signed vz
    // delta toward the throttle target — apply it directly rather than
    // folding it into the lift_drag_force/drag_force equilibrium (see
    // computeThrust's comment: WC3 doesn't model real drag opposing
    // thrust, so combining them here just made the ramp too slow).
    this->acceleration.z = -this->thrust_force;
}
void SCJdynPlane::updateVelocity() {
    float temp{0.0f};
    float dt = GameTimer::getInstance().getDeltaTime();
    this->vx += this->acceleration.x;
    this->vz += this->acceleration.z;

    // Cap forward speed at the ship's own calibrated max (computeThrust's
    // mthrust) so the linear spool-up in computeThrust() can't overshoot
    // once it gets there — matching "1sec:250 2sec:500 and then max" (the
    // ramp keeps accumulating thrust_force indefinitely otherwise, there's
    // no other ceiling on vz).
    if (this->object != nullptr && this->object->entity != nullptr) {
        // Hard ceiling is always the afterburner max — computeThrust's targetSpeed
        // enforces the normal-max ceiling when AB is off, so clamping to thrust_in_newton
        // here would snap the speed instantly when AB is released from full AB speed.
        float hardMax = (float)this->object->entity->weight_in_kg;
        if (hardMax > 0.0f) {
            if (this->vz < -hardMax) this->vz = -hardMax;
            if (this->vz > hardMax) this->vz = hardMax;
        }
    }

    if (this->on_ground && this->status > MEXPLODE) {
        temp = 0.0f;
        float mcos;
        this->vx = 0.0;
        gl_sincos(this->pitch, &temp, &mcos);
        if (mcos == 0.0f) {
            mcos = 0.0001f; // Avoid division by zero
        }
        temp = this->vz * temp / mcos;
        if (this->vy + this->acceleration.y < temp) {
            this->acceleration.y = temp - this->vy;
        }
    }
    this->vy += this->acceleration.y;
}
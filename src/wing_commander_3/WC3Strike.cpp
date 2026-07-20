#include "WC3Strike.h"
#include "WC3GameFlow.h"
#include "WC3Globals.h"
#include "WC3QuitConfirmActivity.h"
#include "WC3NavMap.h"
#include "../realspace/XtreArchive.h"
#include <SDL2/SDL.h>

WC3Strike::WC3Strike() {}
WC3Strike::~WC3Strike() {}

void WC3Strike::onMissionEnded() {
    if (m_ownerGameFlow != nullptr && this->current_mission != nullptr) {
        // Orsini4's own escort objective — confirmed via direct inspection
        // of MISNA004.IFF's real parsed cast: a SKIPMISS (missile) actor and
        // a TTRNSPRT (transport) actor genuinely exist there, matching the
        // mission's own radio text ("Escort transport to Jump Point"). Not
        // gated on scene/mission index — checked generically by actor name
        // instead, since these two specifically-named actors only ever
        // appear in that one mission's cast, so this is a harmless no-op
        // everywhere else. is_destroyed is real, already-maintained state
        // (SCMissionActors::hasBeenHit), not something new being modeled.
        // Locanda2's "chase Flint" mechanic (misnc002.iff, the file first
        // suspected of holding this, genuinely has none — see the historical
        // investigation this session) turned out to live in a DIFFERENT
        // mechanism entirely: misnd002.iff (Laconda2, the real Locanda
        // chapter — see kRosterFlagsByScene's own correction) has a
        // mission-level PROG chunk that itself selects the next mission's
        // filename (OP_SELECT_NEXT_MISSION, see SCenums.h) — branching
        // between the normal "misnd003" and the misnd3bd Flint-rescue side-
        // mission based on in-mission conditions the PROG interpreter
        // already evaluates. Resolve it here (where mission/mission_data are
        // still valid) and hand the raw filename to onMissionComplete, which
        // owns the LOOKMISN lookup and campaign-flow decision.
        for (SCMissionActors* actor : this->current_mission->actors) {
            if (actor == nullptr) continue;
            if (actor->actor_name == "SKIPMISS" && actor->is_destroyed) {
                m_ownerGameFlow->applyShipMoraleDelta(+2);
            } else if (actor->actor_name == "TTRNSPRT" && actor->is_destroyed) {
                m_ownerGameFlow->applyShipMoraleDelta(-4);
            }
        }
        std::string nextMissionOverride;
        int msgIdx = this->current_mission->next_mission_message_index;
        if (msgIdx >= 0 && this->current_mission->mission != nullptr) {
            auto& messages = this->current_mission->mission->mission_data.messages;
            if (msgIdx < (int)messages.size() && messages[msgIdx] != nullptr)
                nextMissionOverride = *messages[msgIdx];
        }
        m_ownerGameFlow->onMissionComplete(this->current_mission->mission_won, nextMissionOverride);
    }
}

SCNavMap* WC3Strike::createNavMap() {
    return new WC3NavMap();
}

void WC3Strike::checkGameSpecificKeyboard() {
    // Manual modifier check rather than the InputAction/SimActionOfst
    // binding system — same reasoning as WC3GameFlow's own Alt+O handler
    // (see its setGlobalHotkeyHandler comment): that system binds plain
    // scancodes to actions, not modifier+key combos.
    bool altHeld = (SDL_GetModState() & KMOD_ALT) != 0;
    if (m_keyboard != nullptr && altHeld && m_keyboard->isKeyJustPressed(SDL_SCANCODE_P)) {
        // User-requested Alt+P pause toggle — added alongside the existing
        // SimActionOfst::PAUSE binding (SDL_SCANCODE_E, see GameEngine.cpp),
        // not a replacement: plain 'P' is already MDFS_POWER, so pause can
        // only live here as a genuine Alt+key combo, not a rebind of P
        // itself.
        this->pause_simu = !this->pause_simu;
    }
    if (m_keyboard != nullptr && altHeld && m_keyboard->isKeyJustPressed(SDL_SCANCODE_W)) {
        // User-requested Alt+W wireframe toggle. SCRenderer::wireframeMode
        // already existed and is already applied every frame (glPolygonMode,
        // SCRenderer.cpp) — it just had no keybinding to actually flip it.
        SCRenderer::instance().wireframeMode = !SCRenderer::instance().wireframeMode;
    }
    if (!altHeld || m_keyboard == nullptr || !m_keyboard->isKeyJustPressed(SDL_SCANCODE_X)) {
        return;
    }
    // Don't stack a second prompt if one's already up (e.g. key repeat, or
    // some other overlay already on top) — mirrors Alt+O's own toggle
    // guard in WC3GameFlow.
    if (Game->hasActivity() && dynamic_cast<WC3QuitConfirmActivity*>(Game->getCurrentActivity())) {
        return;
    }
    auto* activity = new WC3QuitConfirmActivity();
    activity->init();
    Game->addActivity(activity);
}

bool WC3Strike::setMission(char const* missionName) {
    if (Mixer.isSoundPlaying(5)) {
        Mixer.stopSound(5);
    }
    Mixer.stopMusic();

    if (this->current_mission != nullptr) {
        this->current_mission->cleanup();
        delete this->current_mission;
        this->current_mission = nullptr;
    }

    this->miss_file_name = missionName;
    WC3Mission* wc3Mission = new WC3Mission(missionName, &object_cache);
    this->current_mission = wc3Mission;
    ai_planes.clear();
    ai_planes.shrink_to_fit();

    // Configure renderer for space or ground
    SCRenderer& renderer = SCRenderer::instance();
    if (wc3Mission->is_space_mission) {
        renderer.show_ground = false;
        renderer.show_starfield = true;
        renderer.show_fog = false;
        renderer.show_clouds = false;
    } else {
        renderer.show_ground = true;
        renderer.show_starfield = false;
    }
    // Space-scene skybox/stars (WRLD>SKYS/STAR) — loaded for every mission,
    // not just space ones, so still guard on world being non-null. Points
    // directly into wc3Mission's own storage; valid for as long as this
    // mission stays current_mission.
    if (wc3Mission->world != nullptr) {
        renderer.skyFaces = &wc3Mission->world->skyFaces;
        renderer.skyEntities = &wc3Mission->skyEntities;
        renderer.starSprites = &wc3Mission->world->starSprites;
        // WRLD>BACK's palette index (see RSWorld::backgroundColorIndex) —
        // the main VGA palette already stores 8-bit-converted colors (see
        // RSPalette::parsePALT_PALT's convertFrom6To8 calls), so no further
        // scaling is needed beyond mapping 0-255 to GL's 0-1 float range.
        Texel backColor = VGA.getPalette()->colors[wc3Mission->world->backgroundColorIndex];
        renderer.spaceBackgroundColor = {backColor.r / 255.0f, backColor.g / 255.0f, backColor.b / 255.0f};
        // Grey motion-dust particles (WRLD>DUST) — see SCRenderer.h's
        // show_dust/dustCount/dustSpawnRadius comments. hasDust is false
        // only for the 5 ground-mission worlds (no DUST chunk at all —
        // see RSWorld.h's infoField1 comment), which already have
        // show_starfield=false above, so show_dust itself (toggled per-
        // frame in SCStrike::runFrame() based on docked state) ends up
        // correctly false for them either way; still leave the renderer's
        // count/radius at their existing defaults rather than a bogus 0
        // if hasDust is ever false for a space mission.
        if (wc3Mission->world->hasDust) {
            renderer.dustCount = wc3Mission->world->dustCount;
            renderer.dustSpawnRadius = wc3Mission->world->dustSpawnRadius;
        }
        // Old mission's particle positions are meaningless in this one's
        // coordinate space — clear so renderSpaceDust() respawns them
        // fresh around the player instead of drawing them wherever they
        // were left (potentially far outside the new mission's own scale).
        renderer.dustPositions.clear();
        size_t nonNullSkyEntities = 0;
        size_t firstSkyEntVerts = 0;
        for (auto* e : wc3Mission->skyEntities) {
            if (e == nullptr) continue;
            if (nonNullSkyEntities == 0) firstSkyEntVerts = e->vertices.size();
            nonNullSkyEntities++;
        }
        printf("WC3Strike: skyFaces=%zu skyEntities=%zu (non-null=%zu) starSprites=%zu vertsOfFirstSkyEnt=%zu\n",
               wc3Mission->world->skyFaces.size(), wc3Mission->skyEntities.size(), nonNullSkyEntities,
               wc3Mission->world->starSprites.GetNumImages(), firstSkyEntVerts);
    } else {
        renderer.skyFaces = nullptr;
        renderer.skyEntities = nullptr;
        renderer.starSprites = nullptr;
        renderer.spaceBackgroundColor = {0.0f, 0.0f, 0.0f};
    }

    if (this->current_mission->player == nullptr) {
        printf("WC3Strike: No player found in mission '%s'\n", missionName);
        return false;
    }
    if (this->current_mission->player->plane == nullptr) {
        // WC3Mission still tracks this actor as the player (e.g. so mission
        // scripting/on_is_activated triggers work) even when no plane could
        // be built for it — see WC3Mission.cpp's PLAYER-slot branches, the
        // "no plane built" case fires when the ship entity has no
        // DYNM>FGTR chunk. Everything below assumes a plane exists, so bail
        // out gracefully here instead of dereferencing a null player_plane.
        printf("WC3Strike: Player found but no plane could be built for mission '%s'\n", missionName);
        return false;
    }

    MISN_PART* playerCoord = this->current_mission->player->object;
    this->area = this->current_mission->area;
    new_position.x = playerCoord->position.x;
    new_position.z = playerCoord->position.z;
    new_position.y = playerCoord->position.y;

    camera = renderer.getCamera();
    camera->SetPosition(&new_position);
    this->current_target = 0;
    if (!this->current_mission->enemies.empty()) {
        this->target = this->current_mission->enemies[this->current_target];
    }
    this->nav_point_id = 0;
    this->player_plane = this->current_mission->player->plane;
    this->player_plane->yaw = (360 - playerCoord->azymuth) * 10.0f;
    this->player_plane->object = playerCoord;

    // Missions that start the player already in flight (mid-air over a
    // runway, or dropped straight into open space) need a starting
    // throttle/velocity so they don't fall out of the sky or sit dead in
    // space — but a mission that starts the player docked inside a
    // capital ship's hangar bay (flight_deck_entity, see SCStrike.cpp's
    // actor render loop / getDockedCarrierYawTenths) should start
    // stationary instead, matching the reference hangar-launch sequence
    // (confirmed by live testing: forcing throttle=100 here made the ship
    // drift forward through the hangar with no player input at all).
    float dockedYawTenthsUnused;
    bool startsDocked = this->getDockedCarrierYawTenths(dockedYawTenthsUnused);
    if (!startsDocked) {
        if (!wc3Mission->is_space_mission && this->area != nullptr) {
            float ground = this->area->getY(new_position.x, new_position.z);
            if (ground < new_position.y) {
                this->player_plane->SetThrottle(100);
                this->player_plane->SetWheel();
                this->player_plane->vz = -20;
                this->player_plane->Simulate();
            }
        } else {
            this->player_plane->SetThrottle(100);
            this->player_plane->vz = -20;
            this->player_plane->Simulate();
        }
    }

    // Diagnostic: confirms whether the player ship's SSHP>WEAP>FGTR>GUNS/
    // MISL chunks were parsed at all before InitLoadout() consumes them --
    // both zero here means the chunk dispatch never reached the weapon
    // parser (a different bug than weapon-id resolution failing inside it).
    if (this->player_plane->object != nullptr && this->player_plane->object->entity != nullptr) {
        printf("WC3Strike: player ship entity hpts=%zu weaps=%zu before InitLoadout()\n",
               this->player_plane->object->entity->hpts.size(),
               this->player_plane->object->entity->weaps.size());
    }
    this->player_plane->InitLoadout();
    this->player_prof = this->current_mission->player->profile;
    // The mission itself names the cockpit variant to use (e.g. "MEDPIT",
    // MISN's own COCK chunk) — more authoritative than, and not necessarily
    // matching, whatever the player's ship entity declares on its own.
    // Override before SCCockpit::init() resolves it.
    if (!wc3Mission->mission->mission_data.cockpit_name.empty() &&
        playerCoord->entity != nullptr) {
        playerCoord->entity->cockpit_name = wc3Mission->mission->mission_data.cockpit_name;
    }
    this->cockpit = new SCCockpit();
    this->cockpit->player_plane = this->player_plane;
    this->cockpit->init();
    this->cockpit->current_mission = this->current_mission;
    this->cockpit->nav_point_id = &this->nav_point_id;

    // The mission's own MISN>TUNE chunk (mission_data.tune) selects a music
    // track — but not from Strike Commander's bank-based RSMusic/AMUSIC.PAK
    // system (that's SC-only and was never populated for WC3, hence always
    // "No music found"). WC3 mission themes live in the same GMMUSIC.IFF pak
    // WC3GameFlow uses for room/menu music (see loadGameflowMusic), at
    // indices 0-5 (full-length themes, distinct from the short room-ambient
    // loops at indices 24-29) — tune indexes directly into that range.
    //
    // Routed through m_ownerGameFlow->playMissionMusic() (the onboard-
    // carrier ready rooms' own music mechanism, confirmed working) rather
    // than loading a second independent copy of GMMUSIC.IFF and playing it
    // through RSMixer's own separate Mix_Music* bookkeeping — two trackers
    // of "what's currently playing" could disagree (e.g. WC3GameFlow's own
    // m_currentMusicTrack going stale while a mission's music is actually
    // what's loaded), leaving the wrong track playing once control returns
    // to a ready room. Training missions have no owner GameFlow (see
    // launchTrainingMission's comment on why setOwnerGameFlow is skipped
    // there), so this still falls back to the same self-contained
    // pak+RSMixer path for that one caller.
    if (m_ownerGameFlow != nullptr) {
        m_ownerGameFlow->playMissionMusic((int)wc3Mission->mission->mission_data.tune);
    } else {
        Mixer.stopMusic();
        if (!missionMusicPakLoaded) {
            const char* path = "..\\..\\DATA\\SOUND\\GMMUSIC.IFF";
            TreEntry* e = Assets.GetEntryByName(path);
            bool owned = false;
            if (e == nullptr) {
                e = XtreArchive::LoadSingleEntry("gameflow.tre", path);
                owned = e != nullptr;
            }
            if (e != nullptr) {
                missionMusicPak.load(e->data, e->size);
                if (owned) { delete[] e->data; delete e; }
            } else {
                printf("WC3Strike: %s not found\n", path);
            }
            missionMusicPakLoaded = true;
        }
        const std::vector<uint8_t>* midi = missionMusicPak.getTrackMidi(wc3Mission->mission->mission_data.tune);
        if (midi != nullptr) {
            Mixer.playMidiFromMemory(midi->data(), midi->size());
        } else {
            printf("WC3Strike: mission music track %d not found/convertible\n", (int)wc3Mission->mission->mission_data.tune);
        }
    }

    verticalOffset = 0.0f;
    SCStrike::verticalOffset = 0.0f;
    renderer.verticalOffset = verticalOffset;
    renderer.initRenderCameraView();
    return true;
}

void WC3Strike::updateCloakEffect() {
    if (this->player_plane == nullptr) return;

    float dt = 1.0f / 60.0f;
    float rampSpeed = 2.0f;

    if (this->player_plane->cloaked) {
        this->player_plane->cloak_factor += rampSpeed * dt;
        if (this->player_plane->cloak_factor > 1.0f)
            this->player_plane->cloak_factor = 1.0f;
    } else {
        this->player_plane->cloak_factor -= rampSpeed * dt;
        if (this->player_plane->cloak_factor < 0.0f)
            this->player_plane->cloak_factor = 0.0f;
    }

    if (this->player_plane->cloak_factor > 0.0f) {
        RSVGA& vga = RSVGA::instance();
        VGAPalette* colorPal = vga.getPalette();
        if (bwPalette != nullptr && colorPal != nullptr) {
            vga.interpolerPalettes(colorPal, bwPalette, this->player_plane->cloak_factor);
        }
    }
}

// Ctrl+E. Not run through InputActionSystem/SimActionOfst (no modifier-key
// support there) — a direct SDL_GetModState() check alongside the existing
// per-key isKeyJustPressed is simpler than adding modifier support to the
// shared binding system for a single key.
void WC3Strike::checkEjectInput() {
    if (this->is_ejecting || this->is_dying || m_keyboard == nullptr) {
        return;
    }
    if (!(SDL_GetModState() & KMOD_CTRL)) {
        return;
    }
    if (!m_keyboard->isKeyJustPressed(SDL_SCANCODE_E)) {
        return;
    }
    if (this->cockpit == nullptr || this->cockpit->cockpit == nullptr ||
        this->cockpit->cockpit->ejectFrames.GetNumImages() == 0) {
        return;
    }
    this->is_ejecting = true;
    this->eject_fps_counter = 0;
    this->eject_current_frame = 0;
}

// Ctrl+G: toggles gun-pair sync (see SCPlane::guns_synchronized's own
// comment). WC3's real default is desynchronized fire.
void WC3Strike::checkGunSyncToggleInput() {
    if (m_keyboard == nullptr || this->player_plane == nullptr) {
        return;
    }
    if (!(SDL_GetModState() & KMOD_CTRL)) {
        return;
    }
    if (!m_keyboard->isKeyJustPressed(SDL_SCANCODE_G)) {
        return;
    }
    this->player_plane->guns_synchronized = !this->player_plane->guns_synchronized;
}

void WC3Strike::loadGameplaySamplePak() {
    if (this->gameplaySamplePakLoaded) {
        return;
    }
    this->gameplaySamplePakLoaded = true;
    const char* path = "..\\..\\DATA\\SOUND\\SAMPLES.IFF";
    TreEntry* e = Assets.GetEntryByName(path);
    bool owned = false;
    if (e == nullptr) {
        e = XtreArchive::LoadSingleEntry("gameflow.tre", path);
        owned = e != nullptr;
    }
    if (e == nullptr) {
        printf("WC3Strike: %s not found\n", path);
        return;
    }
    this->gameplaySamplePak.load(e->data, e->size);
    if (owned) { delete[] e->data; delete e; }
}

void WC3Strike::playGameplaySample(int index) {
    const std::vector<uint8_t>* wav = this->gameplaySamplePak.getSampleWav(index);
    if (wav == nullptr) {
        printf("WC3Strike: gameplay sample %d not found in SAMPLES.IFF\n", index);
        return;
    }
    Mixer.playSoundVoc(const_cast<uint8_t*>(wav->data()), wav->size());
}

// SOUND/SAMPLES.IFF gun-fire indices, by RSEntity::weapon_display_name (the
// GUNS>DATA display name -- see SCSimulatedObject.cpp's getGunBoltColor for
// why this is the right key instead of the .IFF filename: several files
// share the same display name, e.g. all "Laser" variants).
static int GetGunFireSampleIndex(const std::string &weaponDisplayName) {
    if (weaponDisplayName == "Photon Gun") return 23;
    if (weaponDisplayName == "Laser") return 24;
    if (weaponDisplayName == "Neutron Gun") return 25;
    if (weaponDisplayName == "Ion Gun") return 26;
    if (weaponDisplayName == "Plasma Gun") return 27;
    if (weaponDisplayName == "Reaper Gun") return 28;
    if (weaponDisplayName == "Meson Gun") return 31;
    if (weaponDisplayName == "Tachyon Gun") return 32;
    return -1;
}

void WC3Strike::checkWeaponFireSounds() {
    if (this->player_plane == nullptr) {
        return;
    }
    loadGameplaySamplePak();
    size_t count = this->player_plane->weaps_object.size();
    if (count > this->lastWeapsObjectCount) {
        for (size_t i = this->lastWeapsObjectCount; i < count; i++) {
            SCSimulatedObject *fired = this->player_plane->weaps_object[i];
            if (fired == nullptr || fired->obj == nullptr) {
                continue;
            }
            if (fired->obj->entity_type == EntityType::missiles) {
                playGameplaySample(44);
            } else if (fired->obj->entity_type == EntityType::tracer) {
                int sampleIndex = GetGunFireSampleIndex(fired->obj->weapon_display_name);
                if (sampleIndex >= 0) {
                    playGameplaySample(sampleIndex);
                }
            }
        }
    }
    this->lastWeapsObjectCount = count;
}

void WC3Strike::updateEjectSequence() {
    if (!this->is_ejecting) {
        return;
    }
    if (this->cockpit == nullptr || this->cockpit->cockpit == nullptr) {
        this->is_ejecting = false;
        return;
    }
    size_t numFrames = this->cockpit->cockpit->ejectFrames.GetNumImages();
    if (this->eject_current_frame + 1 >= (int)numFrames) {
        // Hold on the last frame rather than looping/exiting — what happens
        // after ejection (mission failure, respawn, ...) isn't wired up yet.
        return;
    }
    this->eject_fps_counter++;
    if (this->eject_fps_counter > 6) {
        this->eject_fps_counter = 0;
        this->eject_current_frame++;
    }
}

void WC3Strike::renderEjectSequence() {
    if (!this->is_ejecting || this->cockpit == nullptr || this->cockpit->cockpit == nullptr) {
        return;
    }
    RSImageSet& frames = this->cockpit->cockpit->ejectFrames;
    if ((size_t)this->eject_current_frame >= frames.GetNumImages()) {
        return;
    }
    VGA.activate();
    this->cockpit->RenderCockpitOverlayShape(frames.GetShape(this->eject_current_frame));
}

// Polled after SCStrike::runFrame() (not before) so the same frame that
// SCSimulatedObject::Simulate() flips object->alive=false via hasBeenHit()
// also starts the death sequence — zero frames of latency, rather than
// waiting until the next frame to notice.
void WC3Strike::checkDeathTrigger() {
    if (this->is_dying || this->is_ejecting) {
        return;
    }
    if (this->current_mission == nullptr || this->current_mission->player == nullptr ||
        this->current_mission->player->object == nullptr) {
        return;
    }
    if (this->current_mission->player->object->alive) {
        return;
    }
    if (this->cockpit == nullptr || this->cockpit->cockpit == nullptr ||
        this->cockpit->cockpit->deathFrames.GetNumImages() == 0) {
        return;
    }
    this->is_dying = true;
    this->death_fps_counter = 0;
    this->death_current_frame = 0;
}

void WC3Strike::updateDeathSequence() {
    if (!this->is_dying) {
        return;
    }
    if (this->cockpit == nullptr || this->cockpit->cockpit == nullptr) {
        this->is_dying = false;
        return;
    }
    size_t numFrames = this->cockpit->cockpit->deathFrames.GetNumImages();
    if (this->death_current_frame + 1 >= (int)numFrames) {
        // Hold on the last frame — same rationale as updateEjectSequence:
        // what happens after (mission failure, ...) isn't wired up yet.
        return;
    }
    this->death_fps_counter++;
    if (this->death_fps_counter > 6) {
        this->death_fps_counter = 0;
        this->death_current_frame++;
    }
}

void WC3Strike::renderDeathSequence() {
    if (!this->is_dying || this->cockpit == nullptr || this->cockpit->cockpit == nullptr) {
        return;
    }
    RSImageSet& frames = this->cockpit->cockpit->deathFrames;
    if ((size_t)this->death_current_frame >= frames.GetNumImages()) {
        return;
    }
    VGA.activate();
    this->cockpit->RenderCockpitOverlayShape(frames.GetShape(this->death_current_frame));
}

void WC3Strike::runFrame(void) {
    updateCloakEffect();
    checkEjectInput();
    checkGunSyncToggleInput();
    checkWeaponFireSounds();
    updateEjectSequence();
    updateDeathSequence();
    if (this->is_ejecting || this->is_dying) {
        this->pause_simu = true;
    }
    // Cockpit rendering must still run normally — the eject/death flip-books
    // are composited on top of it afterward, not a replacement for it (see
    // renderEjectSequence's header comment). checkDeathTrigger() runs after,
    // not before, so it can catch object->alive flipping false during this
    // same call.
    SCStrike::runFrame();
    checkDeathTrigger();
    renderEjectSequence();
    renderDeathSequence();
    renderDebugSpeedHud();
}
void WC3Strike::renderDebugSpeedHud() {
    if (this->player_plane == nullptr) {
        return;
    }
    WC3Font* font = WC3Globals::getInstance().getFont("GFSM");
    if (font == nullptr || !font->isLoaded()) {
        return;
    }
    // vz is the same raw unit computeThrust()/updateVelocity() calibrate
    // against RSEntity::thrust_in_newton / weight_in_kg (confirmed "KPS" in
    // the game manual), so show it directly rather than the derived
    // airspeed-in-knots value — lets the throttle/afterburner ramp be
    // compared straight against the manual's own numbers (e.g. Arrow:
    // 250 at 1s, 500 at 2s, 520 max).
    char buf[128];
    snprintf(buf, sizeof(buf), "THR:%3d%%  SPD:%4d KPS  AFTB:%s",
             this->player_plane->GetThrottle(), (int)fabsf(this->player_plane->vz),
             this->player_plane->afterburner_engaged ? "ON" : "off");
    FrameBuffer* fb = VGA.getFrameBuffer();
//    font->drawText(fb, buf, 10, 10);
    VGA.vSync();
}

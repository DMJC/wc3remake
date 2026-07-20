#pragma once
#include "../strike_commander/precomp.h"
#include "WC3Mission.h"
#include "WC3MusicPak.h"
#include "WC3SamplePak.h"

class WC3GameFlow;

class WC3Strike : public SCStrike {
public:
    WC3Strike();
    ~WC3Strike();
    // Returns false (and leaves the activity unfit to run — caller must not
    // proceed to runFrame()) if the mission has no resolvable player actor,
    // e.g. its PLAYER cast entry's profile IFF couldn't be loaded.
    bool setMission(char const* missionName);
    void runFrame(void);

    // Set by WC3GameFlow::launchMission() right after construction, so this
    // instance can report the mission outcome back to it — see
    // onMissionEnded() below. Not owned; WC3GameFlow outlives every mission
    // it launches (it stays on the activity stack, unfocused, underneath).
    void setOwnerGameFlow(WC3GameFlow* owner) { m_ownerGameFlow = owner; }

protected:
    // Overrides SCStrike's empty hook: forwards this mission's outcome to
    // WC3GameFlow before SCStrike::runFrame() stops this activity (and, one
    // GameEngine::run() loop iteration later, deletes it — see SCStrike.h's
    // own comment on this hook for why that ordering matters).
    void onMissionEnded() override;
    // Alt+X: pushes WC3QuitConfirmActivity (wing_commander_3/ — see
    // SCStrike.h's checkGameSpecificKeyboard comment for why this can't
    // live directly in the shared SCStrike::checkKeyboard() instead).
    void checkGameSpecificKeyboard() override;
    // Hands back a WC3NavMap (wing_commander_3/) instead of a plain
    // SCNavMap — see SCNavMap::drawWC3TextOverlay's own comment.
    SCNavMap* createNavMap() override;
    // No-op: SCStrike's default plays SC1's bank-based victory/defeat/
    // in-combat music via Mixer.playMusic(uint32_t) (RSMusic/AMUSIC.PAK),
    // a bank that's never populated for WC3 — left unguarded this spammed
    // "No music found for index N in bank 0" every frame. WC3 mission
    // music is set once at mission start (see setMission's playMissionMusic
    // call) and there's no per-combat-state WC3 track wired up yet to
    // switch to here.
    void updateMissionMusic() override {}

private:
    WC3GameFlow* m_ownerGameFlow{nullptr};
    RSVGA& VGA = RSVGA::getInstance();
    void updateCloakEffect();
    VGAPalette savedColorPalette;
    VGAPalette* bwPalette{nullptr};

    // Ejection: Ctrl+E plays RSCockpit::ejectFrames (the cockpit-family
    // "meddeath.iff"-style asset's full 640x480-canvas "1.11" flip-book —
    // see parseWC3_COCK_DETH) composited over the still-rendering cockpit
    // via SCCockpit::RenderCockpitOverlayShape, at the same ~10fps
    // flip-book rate SCExplosion::render() uses (advance one frame every 6
    // game ticks). Only flight-sim simulation/mission update is paused
    // (via the existing pause_simu flag) — the cockpit itself must keep
    // rendering normally underneath the overlay. What happens after the
    // sequence finishes (mission failure, respawn, ...) isn't wired up yet
    // — it just holds on the last frame.
    bool is_ejecting{false};
    int eject_fps_counter{0};
    int eject_current_frame{0};
    void checkEjectInput();
    void updateEjectSequence();
    void renderEjectSequence();

    // Ctrl+G: toggles SCPlane::guns_synchronized. Same rationale as
    // checkEjectInput() above (Ctrl+E) for going straight to
    // SDL_GetModState() instead of the shared InputActionSystem/
    // SimActionOfst binding path, which has no modifier-key support.
    void checkGunSyncToggleInput();

    // GAMEFLOW/SOUND/SAMPLES.IFF: gun-fire/missile-fire sample indices,
    // identified by ear (not from any documented source -- see the session
    // that added this). 8-bit unsigned PCM at 11025 Hz, unlike GFSAMPLE.IFF's
    // 16-bit/22050 Hz (WC3SamplePak's sampleRate/bitsPerSample constructor
    // params exist for this reason).
    WC3SamplePak gameplaySamplePak{11025, 8};
    bool gameplaySamplePakLoaded{false};
    void loadGameplaySamplePak();
    void playGameplaySample(int index);
    // Polls player_plane->weaps_object for newly appended entries each frame
    // rather than hooking SCPlane::Shoot() directly: SCPlane is shared with
    // Strike Commander (strike_commander/, no WC3-specific subclass exists
    // to hang a virtual override off), so detecting new entries from here
    // (100% WC3-side code) avoids adding a WC3-only sound call into shared
    // engine code.
    size_t lastWeapsObjectCount{0};
    void checkWeaponFireSounds();

    // Death: same overlay-compositing mechanism and flip-book cadence as
    // eject above, but plays RSCockpit::deathFrames and triggers
    // automatically instead of on a keypress — SCMissionActorsPlayer::
    // hasBeenHit() (previously a no-op stub) now applies real damage via
    // the shared SCMissionActors::hasBeenHit(), which sets
    // current_mission->player->object->alive = false exactly once;
    // checkDeathTrigger() polls that each frame. Mutually exclusive with
    // eject (whichever starts first wins; see the guards in
    // checkEjectInput()/checkDeathTrigger()) — a dead pilot doesn't eject,
    // and an already-ejecting one doesn't play the death flip-book.
    bool is_dying{false};
    int death_fps_counter{0};
    int death_current_frame{0};
    void checkDeathTrigger();
    void updateDeathSequence();
    void renderDeathSequence();
    // Temporary debug HUD for calibrating throttle/afterburner feel — text
    // overlay of current throttle % and airspeed. SCCockpit's own
    // font/big_font (RSFontManager's SC-specific SHUDFONT.SHP/HUDFONT.SHP)
    // are null for WC3 missions (WC3 ships no equivalent font asset), so
    // this uses WC3's own WC3Globals/WC3Font system instead, drawn as a
    // second pass into the same software framebuffer + an extra VGA.vSync()
    // after SCStrike::runFrame()'s own cockpit render already completed one.
    void renderDebugSpeedHud();
    // Mission music ("TUNE" chunk, MISN::mission_data.tune) is played from
    // the same GMMUSIC.IFF pak WC3GameFlow uses for room/menu music (see
    // WC3GameFlow::loadGameflowMusic/playGameflowMusic) — indices 0-5 are
    // full-length mission themes, distinct from the short room-ambient
    // loops at indices 24-29. Loaded once and reused across missions.
    WC3MusicPak missionMusicPak;
    bool missionMusicPakLoaded{false};
};

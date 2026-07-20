#pragma once
#include <array>
#include <unordered_set>
#include "../strike_commander/precomp.h"
#include "WC3Scene.h"
#include "WC3BranchPak.h"
#include "WC3MusicPak.h"
#include "WC3SamplePak.h"
#include "WC3Strike.h"
#include "WC3Options.h"
#include "WC3SaveGame.h"
#include "WC3LoadoutData.h"

class WC3GameFlow : public IActivity {
public:
    WC3GameFlow();
    ~WC3GameFlow();

    void init();
    void runFrame(void);

    // Called by WC3Strike::onMissionEnded() the instant a mission's outcome
    // is known — see that hook's own comment for why this can't instead be
    // read back from WC3GameFlow's own runFrame() after control returns
    // (the WC3Strike instance, and its current_mission, are already deleted
    // by then).
    //
    // Win path implemented: LOOKMISN.IFF's mission index N maps 1:1 to
    // scene N (SCEN0000 -> mission 0 -> SCEN0001 -> mission 1 -> ...), so a
    // win just advances current_scene to m_selectedMissionIndex + 1 and
    // queues a reload. Loss path NOT implemented — it branches to a
    // different mission track entirely (e.g. LOOKMISN indices 49/50,
    // MISNR001/MISNR002) via a per-mission rule that isn't confirmed yet.
    //
    // nextMissionOverrideFilename: the just-finished mission's own PROG may
    // select an explicit next-mission filename via OP_SELECT_NEXT_MISSION
    // (see SCMission::next_mission_message_index) — WC3Strike::onMissionEnded
    // resolves the raw message string and passes it through here (empty =
    // no override, the normal case). If it resolves to a LOOKMISN index
    // other than the plain sequential m_selectedMissionIndex+1 — confirmed
    // for Laconda2 (misnd002.iff) branching to the misnd3bd Flint-rescue
    // side-mission — the normal scene advance is deferred and that mission
    // is flown first (State::LAUNCH_EXTRA_MISSION), with the campaign
    // resuming at the originally-deferred scene once IT completes. See that
    // state's own comment for why this can't launch synchronously here.
    void onMissionComplete(bool won, const std::string& nextMissionOverrideFilename = std::string());

    // Applies a ship-wide morale delta from an in-flight mission event —
    // e.g. WC3Strike::onMissionEnded() checking Orsini4's own SKIPMISS/
    // TTRNSPRT actors before calling onMissionComplete() above (that
    // mission's outcome isn't visible to WC3GameFlow itself, only to
    // WC3Strike while current_mission is still alive — same reasoning as
    // onMissionComplete's own comment). Public (not folded into
    // applyBranchEffects, which is branch-choice-table-driven and private)
    // since this is the one morale-affecting path that isn't a dialogue
    // choice. Shares applyBranchEffects' own "ship morale fans out to every
    // character" rule — see that method's comment.
    void applyShipMoraleDelta(int delta);

    // Plays a GMMUSIC.IFF track by index through this same GameFlow's own
    // musicPak/m_currentMusicPtr/m_currentMusicTrack — the exact mechanism
    // the onboard-carrier ready rooms use (see playGameflowMusic/
    // playMusicForRoom) — rather than a mission activity loading and
    // tracking a second, independent copy of the same file. WC3Strike calls
    // this with the mission's own MISN>TUNE index (mission_data.tune) when
    // it has an owner GameFlow to route through (see setOwnerGameFlow);
    // sharing this one Mix_Music*/track-index bookkeeping is what lets
    // in-flight mission music and post-mission room music hand off to each
    // other correctly instead of the two disagreeing about what's playing.
    void playMissionMusic(int trackIndex) { playGameflowMusic(trackIndex); }

private:
    enum class State {
        PLAY_ORIGIN_MOVIE,
        PLAY_OPENING_MOVIE,
        LOAD_SCENE,
        SCENE_ACTIVE,
        AWAITING_BRANCH_CHOICE,
        MISSION_STUB,
        SIMULATOR,
        CONFIRM_QUIT,
        // Set by onMissionComplete() when the just-finished mission's own
        // PROG data (OP_SELECT_NEXT_MISSION — see SCMission::
        // next_mission_message_index) selected a mission outside the normal
        // sequential campaign walk (e.g. Laconda2 branching to the misnd3bd
        // Flint-rescue side-mission). Deliberately NOT launched synchronously
        // from onMissionComplete/onMissionEnded — that runs while the
        // just-finished WC3Strike is still the active activity, one frame
        // before SCStrike::runFrame()'s own Game->stopTopActivity() call
        // pops it (see WC3Strike::onMissionEnded's comment); pushing a new
        // activity there would make that stopTopActivity() pop the WRONG
        // (newly-launched) one instead. This state defers launchMission()
        // to WC3GameFlow's own next runFrame() tick, by which point the old
        // activity is actually gone and WC3GameFlow is the real top of the
        // stack again — same reasoning LOAD_SCENE already relies on.
        LAUNCH_EXTRA_MISSION,
        DONE
    };

    State state{State::PLAY_ORIGIN_MOVIE};
    int current_scene{0};
    bool savegame_exists{false};

    PakArchive shapePak;
    FrameBuffer* sceneFB{nullptr};
    WC3Scene* scene{nullptr};

    // Options — persisted in config.ini's [Options] section (see
    // WC3Options.h's loadOptions/saveOptions), loaded once in init() and
    // edited via WC3OptionsActivity (the Hub's "controls" tab, or a global
    // Alt+O during flight — see openOptionsActivity()). Replaces what used
    // to be two standalone ad-hoc bools (opt_transition_videos,
    // opt_stub_missions — now m_options.transitionsOn/devStubMissions) so
    // there's one settings mechanism instead of two parallel ones.
    // devStubMissions: while the campaign/gameflow path (scene sequencing,
    // branch choices) is still being worked out, launchMission() shows a
    // simple Win/Loss prompt (State::MISSION_STUB) instead of actually
    // flying the mission — lets the whole scene-progression chain be
    // exercised without the flight engine. Dev-only, no in-game checkbox.
    WC3Options m_options;
    std::string m_stubMissionName;

    // SC_205.MVE is cached on first use; contains all generic room-transition shots.
    std::vector<uint8_t> sc205Data;
    void ensureSC205();
    void playSC205Shot(int shotIndex);

    // LOOKMOVI.IFF's index → movie-basename table (e.g. 24 → "sc_6"), loaded once at
    // init and handed to WC3Scene so it can resolve VAR(45) calls in ACTV bytecode.
    std::unordered_map<int, std::string> m_lookMovi;
    void loadLookMovi();

    // GAMEFLOW/LOOKMISN.IFF's index -> mission-file-basename table (e.g. 0 ->
    // "misna001"), same FORM LOOK / "%04d" chunk shape as LOOKMOVI.IFF.
    // Resolves ActionResult::missionIndex (OP5) to an actual MISSIONS.TRE
    // filename once the player commits to launching (ActionResult::launchMission,
    // OP1). Set by talking to Flight Control's crew (OP5) and consumed later by
    // clicking the post-wingman-selection "Go to FLIGHT DECK." door (OP1) — two
    // separate gump clicks, so the resolved index is cached in m_selectedMissionIndex
    // between them.
    std::unordered_map<int, std::string> m_lookMisn;
    void loadLookMisn();
    int m_selectedMissionIndex{-1};
    // Hands off to mission flight: resolves m_selectedMissionIndex via
    // m_lookMisn and pushes a WC3Strike activity to fly it.
    void launchMission();
    // Set by onMissionComplete() when deferring the normal scene advance to
    // fly an out-of-sequence "extra" mission first (see State::
    // LAUNCH_EXTRA_MISSION and onMissionComplete's own comment) — the scene
    // the campaign should resume at once that extra mission itself
    // completes. -1 when no extra mission is pending (the normal case).
    int m_pendingResumeScene{-1};

    // GAMEFLOW/SIMDESC.IFF: Simulator terminal (room 0009) mission list.
    // FORM SIM_ -> DEF_ chunk: [count:u32LE] then `count` fixed 25-byte
    // records (name[19], right-padded/null-terminated if shorter than 19
    // chars, exactly filling it with no terminator if not — "Training
    // Mission 10" is the one case that happens; reserved[2]; then the
    // LOOKMISN.IFF launch index as u32LE). The BLK_ chunk (whole-campaign
    // mission list, same record shape) isn't used by this screen.
    struct SimMissionEntry { std::string name; int lookmisnIndex; };
    std::vector<SimMissionEntry> m_simMissions;
    void loadSimDesc();

    // GAMEFLOW/BRFDESC.IFF: per-mission briefing description shown in the
    // Simulator terminal's detail view. FORM BRF_ -> one sub-chunk per
    // mission, tagged by its 4-digit decimal LOOKMISN index (e.g. "0055"),
    // payload = [line_count:u32LE] then that many null-terminated lines,
    // already pre-wrapped by the original data (no wrapping needed here).
    // Keyed by LOOKMISN index, same as SimMissionEntry::lookmisnIndex.
    std::unordered_map<int, std::vector<std::string>> m_briefingText;
    void loadBrfDesc();

    // Simulator terminal (room 0009) custom overlay state. Room 0009's own
    // ACTV bytecode only triggers button-press animations (OP44 with a
    // gumpId of -12, decoded as animation/highlight bookkeeping — not a
    // real gump reference) and the "return" door's plain GoRoom 3 — the
    // mission list/detail/launch behaviour itself isn't data-driven at
    // all, so (like MISSION_STUB/CONFIRM_QUIT) it's a hand-built overlay
    // drawn over the room's own frozen background art. Entered from
    // finishGumpAction() when a GoRoom lands on target 9; exited by
    // clicking "return", which is handled directly in the State::SIMULATOR
    // case (not via room 0009's own ACTV) since normal per-frame zone
    // click-handling is bypassed while this state is active.
    bool m_simShowDetail{false};
    int m_simSelectedIndex{0}; // index into m_simMissions
    // "Return" defers its actual GoRoom navigation by a frame: clicking it
    // triggers gump 7's lit-frame animation (triggerGumpAnim) and sets this
    // flag rather than navigating immediately, so that lit frame actually
    // gets rendered+displayed at least once before we leave room 0009 —
    // otherwise the frame flip and the room switch land in the same
    // unrendered frame and the flash is never visible. Consumed by
    // State::SIMULATOR's own per-frame poll of scene->pollFinishedAnimation().
    bool m_simPendingReturn{false};
    // "Run" in the Simulator terminal: launches MISSIONS/tsim%03d.IFF
    // directly (1-based, e.g. simIndex 0 -> tsim001.iff) — NOT resolved via
    // m_lookMisn/LOOKMISN.IFF, since training missions aren't part of the
    // main campaign track. Deliberately does not call setOwnerGameFlow(), so
    // WC3Strike's onMissionEnded() hook stays a no-op and mission end just
    // pops the activity straight back to WC3GameFlow with `state` still
    // State::SIMULATOR — no campaign-progression side effect (no scene
    // advance, no bankA[12] outcome flag), unlike the real launchMission().
    void launchTrainingMission(int simIndex);
    // Set by launchTrainingMission() before entering State::MISSION_STUB,
    // so that stub's Win/Loss keys know to return to State::SIMULATOR
    // (no onMissionComplete()) instead of the normal campaign win/loss path.
    bool m_stubReturnToSimulator{false};
    // Plays MOVIES/<name>.MVE (case-insensitive lookup, uppercased on disk), shot
    // shotIndex. Used for per-character "talk" gumps that name their own dedicated
    // movie via VAR(45), instead of the shared SC_205.MVE. shotIndex is a BRCH-group
    // index (WC3MVEPlayer::playBranchGroup) — the game's own single narrative "shot"
    // can itself span multiple raw SHOT (palette-slot) chunks, e.g. a camera cut
    // within the same take; files with no BRCH structure fall back automatically.
    void playNamedMovieShot(const std::string& name, int shotIndex);
    // Plays a list of INIT/EXIT-triggered movies in order (routes "sc_205" to the
    // cached SC_205 loader, everything else to playNamedMovieShot).
    void playPendingMovies(const std::vector<WC3Scene::PendingMovie>& movies);

    // BRANCH PAKs from cd1-4miss.tre: player dialogue (Blair voice lines + subtitles).
    // Index 0 = BRANCH1.PAK (cd1miss), 1 = BRANCH2.PAK, etc.
    WC3BranchPak branchPaks[4];
    void loadBranchPaks();
    void logRoomMenu() const;

    // GAMEFLOW/SOUND/GMMUSIC.IFF: background MIDI music — one ambient loop track
    // per ready-room-hub location, selected by room ID (see playMusicForRoom's
    // kRoomMusicTrack table; VAR(3) in the bytecode looked plausible but was
    // verified wrong). Halted for the duration of any MVE playback
    // (playMovie/playSC205Shot/playNamedMovieShot) and resumed after.
    WC3MusicPak musicPak;
    int m_currentMusicTrack{-1};
    Mix_Music* m_currentMusicPtr{nullptr};
    void loadGameflowMusic();
    void playGameflowMusic(int trackIndex);
    // Looks up roomId ("%04d") in kRoomMusicTrack and plays that room's ambient
    // track, if any. No-op (leaves current music playing) for rooms with no
    // entry, e.g. Gunnery Control.
    void playMusicForRoom(const std::string& roomId);
    void pauseGameflowMusic();
    void resumeGameflowMusic();
    // RAII guard: halts gameflow music for its scope, resuming on destruction
    // (any return path) — instantiate as a local at the top of any function
    // that plays an MVE, so playback never overlaps with the background score.
    struct MusicPauseScope {
        WC3GameFlow* gf;
        explicit MusicPauseScope(WC3GameFlow* gf) : gf(gf) { gf->pauseGameflowMusic(); }
        ~MusicPauseScope() { gf->resumeGameflowMusic(); }
    };

    // GAMEFLOW/SOUND/GFSAMPLE.IFF: door/UI sound-effect samples, referenced by
    // index from a gump's own FORM SOND/_ID_ chunk (WC3Gump::sound_ids).
    // Triggered on hover-start/hover-end for hover-only actors (e.g. doors),
    // not continuously — see the hover diff in runFrame()'s SCENE_ACTIVE case.
    WC3SamplePak samplePak;
    void loadGameflowSamples();
    void playGameflowSample(int index);
    std::vector<uint32_t> m_prevHoveredActorIds;

    // Deferred gump-click follow-through (subscene/transitionShot/GoRoom), shared
    // between the immediate case (no dialogue choice) and the AWAITING_BRANCH_CHOICE
    // resolution (choice made, then the click's other effects still apply).
    void finishGumpAction(const WC3Scene::ActionResult& action);

    // Lift (room 0005) floor selection. Its own ACTV bytecode implements this
    // with an ambiguous/unreliable gate (a scratch-slot equality check on
    // bank-A slot 8, whose actual write site was never found — see todo.MD)
    // that in practice let a floor click navigate immediately instead of
    // animating, so this is reimplemented directly here rather than trusting
    // that bytecode: gumps 2/3/4/12 (the three floor buttons and Exit LIFT)
    // are intercepted in activateZone() before they'd otherwise run their
    // own ACTV, and driven through this explicit 0-2 state instead.
    // Confirmed via room 0005's own ACTV disassembly: gump 2 (titl "Go to
    // BRIDGE LEVEL.") targets room "0000", gump 3 ("LIVING LEVEL.") targets
    // "0006", gump 4 ("FLIGHT LEVEL.") targets "0003" — hence floor index
    // 0=Flight/1=Living/2=Bridge matching the room order bottom-to-top.
    int m_liftSelectedFloor{-1}; // -1 = not in the lift / not yet seeded
    // True while the current room is the lift (room id "0005") — set on
    // entry, checked by activateZone() to know whether to intercept gumps
    // 2/3/4/12.
    bool m_inLift{false};
    // Lift sequencing state, so the sign/exit only settle once their
    // respective animation has actually finished playing (see
    // handleLiftAnimFinished()) rather than instantly on click:
    //   Idle       — settled; floor buttons and Exit LIFT are live.
    //   Traveling  — gumps 8/9 or 10/11 (travel indicator) are playing;
    //                floor buttons/Exit LIFT clicks are ignored until the
    //                primary gump (8 or 10) reports finished, at which
    //                point the sign (gump 5) updates and phase -> Idle.
    //   DoorOpening — gump 13 (the door, triggered only by clicking Exit
    //                LIFT) is playing; once it reports finished, the
    //                actual GoRoom navigation runs.
    enum class LiftPhase { Idle, Traveling, DoorOpening };
    LiftPhase m_liftPhase{LiftPhase::Idle};
    // Seeds m_liftSelectedFloor from bank-A slot 4 (the room just departed,
    // set by every room's own EXIT code — see WC3Scene::getBankA's comment)
    // and puts the lift's sign/buttons in its settled resting state on the
    // floor the player is actually arriving on (no travel/door animation —
    // we're already there). Called from finishGumpAction() when a GoRoom
    // lands on target 5.
    void enterLiftRoom();
    // Sets the three floor buttons' (gumps 4/3/2, for floor 0/1/2) lit/dark
    // state to reflect floorIndex. Updates immediately on click — unlike
    // the sign, there's no reason to wait for the travel animation to
    // reflect which floor is now requested.
    void setLiftButtonHighlight(int floorIndex);
    // Sets the sign's (gump 5, 3 frames, one per floor) frame to floorIndex.
    // Only called once travel has actually finished (see
    // handleLiftAnimFinished) — the sign should show the floor the lift
    // has arrived at, not the one just requested.
    void setLiftSignFrame(int floorIndex);
    // Plays the sample attached to a TYPE_SOUND actor in the lift room
    // (via its own sound_ids, the same GFSAMPLE.IFF-indexed mechanism
    // hover-triggered door sounds already use). Confirmed via room 0005's
    // ACTV disassembly: gump 2/3/4's own code calls OP50(16,0) exactly when
    // starting travel and OP50(15,0) exactly when settling/opening the
    // door (currently a no-op in the general interpreter — see runBytecode
    // case 50 — since resolving it generally would need ActionResult
    // plumbing WC3Scene doesn't have reason to carry yet; reproduced
    // directly here instead, since this room's specific sound-actor ids
    // are already known). Room actors: id 15->sample 23 ("door"), id
    // 16->sample 24 ("start travel"), matching selectLiftFloor()'s/
    // handleLiftAnimFinished()'s/exitLift()'s own call sites below.
    void playLiftSound(uint32_t soundActorGumpId);
    // Plays LIFT.MVE's travel branch for the direction just requested (branch
    // group 0 going up toward Bridge, group 1 going down toward Flight —
    // matches the same up/down split selectLiftFloor() already uses for the
    // gump 8/9 vs 10/11 travel-indicator animation). Blocks until the clip
    // finishes (WC3MVEPlayer::playBranchGroup is synchronous), so this runs
    // to completion before the rest of selectLiftFloor()'s own travel
    // animation/sign-change sequence starts.
    void playLiftTravelMovie(bool goingUp);
    // Handles a floor-button click: ignored unless m_liftPhase is Idle, and
    // a no-op if floorIndex is already selected (the travel animation
    // should only play "when a different floor to the current floor is
    // selected"). Otherwise updates the button highlight immediately, kicks
    // off the travel-indicator animation on the correct side (gumps 8/9 for
    // up toward Bridge, 10/11 for down toward Flight — confirmed
    // directionality from the ACTV disassembly) and moves to Traveling; the
    // sign updates later, once handleLiftAnimFinished() sees it complete.
    void selectLiftFloor(int floorIndex);
    // Handles "Exit LIFT.": ignored unless m_liftPhase is Idle. Triggers
    // gump 13's door animation and moves to DoorOpening — the actual
    // GoRoom navigation is deferred until handleLiftAnimFinished() sees
    // that animation complete, not run immediately here.
    void exitLift();
    // Routes a scene->pollFinishedAnimation() result for a lift-driven gump
    // (8/9/10/11/13 — see LiftPhase) to its next step, consuming the event
    // so the caller doesn't also runGumpAction() that gump's own (bypassed)
    // ACTV code. Returns false for any gump id it doesn't own, so the
    // caller falls back to the normal chained-animation handling.
    bool handleLiftAnimFinished(uint32_t gumpId);

    // Personnel/"Loadout" Terminal (room 0008) — a multi-page hub (welcome
    // title, Login, Duty Logs) whose own ACTV bytecode can't drive page
    // switching (same reasoning as the Lift/Simulator), so pages are driven
    // explicitly via scene->setGumpVisible() on gump-ids confirmed by
    // decoding room 0008's own SHAPES.PAK art. The "controls" tab doesn't
    // switch to an in-room page at all — it pushes the standalone
    // WC3OptionsActivity (see Phase 4), the same one Alt+O opens during
    // flight, so there's exactly one Options implementation either way.
    enum class TerminalPage { Login, Hub, DutyLogs };
    TerminalPage m_terminalPage{TerminalPage::Login};
    // True while the current room is the terminal (room id "0008") — set on
    // entry, checked by activateZone() to know whether to intercept its tab
    // buttons, mirroring m_inLift.
    bool m_inTerminal{false};
    // The numeric room id the player was in right before this terminal
    // entry (e.g. 3 for Flight Control, 4 for the Bridge, 13 for the
    // Refueling Depot — "Activate MAIN/LOADOUT TERMINAL" reaches room 0008
    // from more than one room, so this can't be a hardcoded constant).
    // Captured fresh on every GoRoom into the terminal (see
    // finishGumpAction()); the Hub's "logoff" navigates back here instead
    // of to a fixed room. Defaults to Flight Control (3) as a fallback in
    // case the terminal is ever entered before this is set some other way.
    int m_terminalOriginRoom{3};

    // Actor gump ids (TYPE_CHARACTER/TYPE_CHARACTER2 only — the "talk to X"
    // portrait family, not the mixed door/hover-anim TYPE_PERSON) the player
    // has clicked since entering the current room. Recorded in
    // runGumpAction(); flushed into m_hiddenActorsByRoom and cleared the
    // moment the player leaves the room for anywhere at all — including
    // into a terminal-type room (Loadout Terminal/Simulator/Loadout/Kill
    // Board), which is just an ordinary GoRoom target like any other room —
    // see finishGumpAction()'s GoRoom branch.
    std::unordered_set<uint32_t> m_talkedToActorsThisVisit;
    // Actors permanently hidden after being talked to, keyed by stage room
    // index (WC3Scene::getCurrentRoom()'s own index space). WC3Scene's
    // gumpVisible override is cleared by every runRoomInit() (room entry,
    // including re-entry), so this has to be reapplied there each time — see
    // finishGumpAction()'s GoRoom branch, right after runRoomInit(). Cleared
    // whenever the whole stage reloads (loadScene()) since room indices
    // aren't stable across different SCEN files (VICTORY.IFF loads rooms out
    // of ID order per-scene — see loadScene()'s own startRoom-lookup
    // comment), same reasoning as shapeCache's clear there.
    std::unordered_map<int, std::unordered_set<uint32_t>> m_hiddenActorsByRoom;

    // Player profile: callsign + whether Login has ever been completed.
    // Persisted in its own file (separate from numbered save slots) so
    // Login's one-shot-forever rule survives process restarts even with no
    // manual save. Loaded once in init(), before any room is entered.
    struct WC3PlayerProfile {
        bool everLoggedIn{false};
        std::string callsign;
    };
    WC3PlayerProfile m_profile;
    void loadProfile();
    void saveProfile();

    // Login page's callsign text field. Created lazily on first Login-page
    // entry (needs m_keyboard, which isn't set until init()).
    std::shared_ptr<Keyboard::TextEditor> m_loginEditor;

    // Entered when a GoRoom lands on target 8. Picks Login vs Hub from
    // m_profile.everLoggedIn and applies that page's gump visibility.
    void enterTerminalRoom();
    // Shows/hides room 0008's known background/button gump-ids to match
    // m_terminalPage. Called from enterTerminalRoom() and every terminal
    // page-transition click.
    void applyTerminalPageVisibility();
    // Login "enter" (Return key, or clicking with non-empty text): commits
    // the entered callsign, marks everLoggedIn, persists the profile, and
    // switches to Hub. No-op while the field is empty.
    void commitLogin();
    // Pushes the standalone Options overlay (WC3OptionsActivity) — used by
    // both the Hub's "controls" tab and the global Alt+O hotkey, so there's
    // exactly one Options implementation. Implemented in Phase 4.
    void openOptionsActivity();
    // Duty Logs: which numbered slot is currently highlighted (1-based) and
    // the scroll offset (first visible slot number) for the list — same
    // "(x,y,w,h) rects computed at layout time, reused for click hit-
    // testing" pattern as State::SIMULATOR's own mission list, since this
    // page (like Simulator) bypasses the normal scene-zone system entirely
    // for its list/buttons — see renderDutyLogsPage().
    int m_dutyLogsSelectedSlot{1};
    int m_dutyLogsScrollOffset{1};
    // "load"/"save" button handlers, operating on m_dutyLogsSelectedSlot.
    void dutyLogsLoad();
    // name: the player-typed save name (see WC3SaveGame::name) — empty is
    // valid (the slot just shows as "GAME<N>" with no name suffix).
    void dutyLogsSave(const std::string& name);
    // True while the save-name text-input overlay (triggered by clicking
    // "save") is up — renderDutyLogsPage() short-circuits its normal list/
    // button click handling while this is set, same reasoning as Login's
    // own text-entry short-circuit in SCENE_ACTIVE. Reuses the same
    // Keyboard::TextEditor mechanism as the Login page's callsign field.
    bool m_dutyLogsNaming{false};
    std::shared_ptr<Keyboard::TextEditor> m_dutyLogsSaveNameEditor;
    // Renders the Duty Logs page's own background/scroll-arrows/load-save-
    // return buttons via scene->render() (like any other room), then
    // overlays the scrollable slot list and handles its own row/button
    // clicks directly — same self-contained approach as State::SIMULATOR,
    // since the list rows aren't gump-zones the normal scene->hitTest()/
    // activateZone() pipeline knows about. Called from runFrame() while
    // m_inTerminal && m_terminalPage==DutyLogs, short-circuiting the normal
    // SCENE_ACTIVE hover/click handling the same way Login does.
    void renderDutyLogsPage();
    // Set by dutyLogsLoad(), applied by the State::LOAD_SCENE case right
    // after the fresh loadScene() it triggers — see that case's own comment
    // for why this can't be applied immediately.
    bool m_hasPendingRestore{false};
    std::unordered_map<int, int32_t> m_pendingRestoreBankA;
    std::unordered_map<int, int32_t> m_pendingRestoreBankB;
    // Kill Board pilot stats — seeded once (see enterKillBoardRoom()) if no
    // save has ever set them, otherwise round-tripped through every Duty
    // Logs save/load (WC3SaveGame's ROST chunk) alongside scene/bank state.
    std::vector<WC3PilotStats> m_pilotStats;

    // The people branch-choice consequences (see kBranchConsequences) can
    // target. The first 7 (Flint..Maniac) are flight-roster pilots, ordered
    // to match loadScene()'s own 143-149 roster-flag comment exactly
    // (143=Flint, 144=Flash, 145=Cobra, 146=Hobbes, 147=Vagabond,
    // 148=Vaquero, 149=Maniac) — kAllRosterFlags there indexes this enum
    // directly for those 7, see kRosterPilotCount. Rachel (the ship's
    // mechanic, a love-interest subplot character — see kBranchConsequences'
    // own comment) is appended after Maniac: she's a morale target like the
    // others but never a flight-roster pilot, so she's deliberately outside
    // the bank-flag-indexed range and must never be looped over by anything
    // that assumes "first N == roster pilots" without checking
    // kRosterPilotCount. Deliberately NOT the same six names as
    // m_pilotStats/the Kill Board (that list omits Flash, who isn't
    // recruited yet at game start, and Rachel isn't a combat pilot at all)
    // — these are different pilot lists entirely.
    enum class WC3Pilot : uint8_t { Flint, Flash, Cobra, Hobbes, Vagabond, Vaquero, Maniac, Rachel, Count };
    // Only the first kRosterPilotCount entries of WC3Pilot correspond to a
    // real bank-B roster-availability flag (kAllRosterFlags in loadScene()
    // has exactly this many elements) — Rachel, appended after, does not.
    static constexpr size_t kRosterPilotCount = 7;
    // A branch choice can force a pilot permanently on/off the flight
    // roster (e.g. 013.IFF false = Flint removed, 014.IFF false = Flint
    // added back) — see m_pilotRosterOverride's own comment for why this
    // can't just be a direct write into bank-B 143-149.
    enum class WC3RosterOverride : uint8_t { None, ForceUnavailable, ForceAvailable };
    // One consequence of picking a branch-choice line (see
    // kBranchConsequences). pilot/rosterPilot are only meaningful when
    // pilotMoraleDelta/rosterOverride are actually set — Count/None are the
    // "no target" defaults, not real pilots, so a zero-initialized effect
    // is inert.
    struct WC3BranchEffect {
        WC3Pilot pilot{WC3Pilot::Count};
        int      pilotMoraleDelta{0};
        int      shipMoraleDelta{0};
        WC3Pilot rosterPilot{WC3Pilot::Count};
        WC3RosterOverride rosterOverride{WC3RosterOverride::None};
        // Subsumes the old isExcaliburChoice special case: -1 = no override.
        int      missionIndexOverride{-1};
        // Flint/Rachel love-triangle: a direct assignment (NOT a delta like
        // pilotMoraleDelta) into m_pilotRelationshipStatus — see that
        // member's own comment for the 13/2/0 value meanings. -1 = no
        // assignment (the "no target" default, since 0 is itself a real
        // value here, unlike pilotMoraleDelta where 0 is naturally inert).
        WC3Pilot relationshipPilot{WC3Pilot::Count};
        int      relationshipStatus{-1};
    };
    // A row in kBranchConsequences — which effects fire depending on which
    // line (TRUE/FALSE) the player picked. Most rows have exactly one
    // effect per side; a vector (rather than a single WC3BranchEffect)
    // covers the couple of entries with two simultaneous effects on one
    // side (e.g. 019.IFF lowers Hobbes' morale AND raises Cobra's in the
    // same branch; 023.IFF raises ship morale AND removes Vaquero from the
    // roster) without a special-cased struct shape.
    struct WC3BranchConsequenceRow {
        std::vector<WC3BranchEffect> onTrue, onFalse;
    };
    // Per-character morale (the 7 roster pilots plus Rachel) — an unbounded
    // delta accumulator (the reference data only says "+1"/"-1" per choice,
    // confirmed as a real per-choice unit, not just direction), adjusted by
    // applyBranchEffects(). A shipMoraleDelta effect also nudges every
    // entry here by the same amount, in addition to m_shipMorale itself —
    // user-confirmed rule: ship-wide morale changes affect all characters
    // equally, on top of anything targeting a specific one in the same
    // choice. Not wired to any gameplay/UI consequence yet — this pass
    // tracks and persists it. Round-tripped via WC3SaveGame's MRAL chunk.
    std::array<int, (size_t)WC3Pilot::Count> m_pilotMorale{};
    // Ship-wide morale, same accumulator convention as m_pilotMorale — see
    // that member's comment for how this also fans out to every pilot.
    int m_shipMorale{0};
    // Flint/Rachel love-triangle endgame-companion tracker (029/031/IFF
    // "kiss her" choice, 039/032.IFF the same choice offered for whichever
    // one wasn't approached first — see kBranchConsequences' own comment
    // for the full 4-entry structure). Kissing one sets that pilot's own
    // slot to 13 (chosen companion) and the OTHER's slot to 2 (passed
    // over, since the two are exclusive — kissing one forecloses the
    // other). Declining just applies a -1 pilotMoraleDelta (the ordinary
    // morale accumulator, not this array) and leaves both slots at their
    // default 0 (undetermined) — if the player declines both offers, both
    // stay 0, meaning "alone" for the endgame cutscene once/if that's
    // wired up. Indexed like m_pilotMorale; only Flint's and Rachel's
    // slots are ever actually written by the current table. Not wired to
    // any gameplay/UI consequence yet (no endgame cutscene exists in this
    // engine) — tracked and persisted for whenever it does. Round-tripped
    // via WC3SaveGame's MRAL chunk alongside pilotMorale.
    std::array<int, (size_t)WC3Pilot::Count> m_pilotRelationshipStatus{};
    // Permanent roster overrides from branch choices (e.g. Flint
    // removed/re-added) — plain WC3GameFlow members, NOT bank-A/B slots.
    // Bank slots 12/143-149/155/191 etc. are real WC3 VM variable addresses
    // recovered from decompiled bytecode; morale/roster-override are
    // engine-side approximations with no bytecode backing, so they get
    // their own storage instead of squatting on that address space (same
    // reasoning m_pilotStats/m_loadoutShipIndex already follow). The
    // tradeoff: loadScene() unconditionally resets bank-B 143-149 from its
    // own per-scene defaults table every mission cycle (see
    // kRosterFlagsByScene), which would silently undo a removal/re-add —
    // so loadScene() reapplies these overrides as a correction immediately
    // after that reset (see loadScene()'s own comment at the site).
    std::array<WC3RosterOverride, (size_t)WC3Pilot::Count> m_pilotRosterOverride{};
    // Applies one branch choice's worth of consequences (whichever side —
    // TRUE or FALSE — the player picked) — see commitChoice()'s use.
    void applyBranchEffects(const std::vector<WC3BranchEffect>& effects);
    // True while the current room is the Kill Board (room id "0011") — set
    // on entry, checked by runFrame() the same way m_inTerminal/m_inLift are.
    bool m_inKillBoard{false};
    // Session-only: "logoff" sets this so the Kill Board shows NOT LOGGED
    // IN for the rest of this run, without touching the persisted callsign
    // (Login is one-shot-forever — logoff can't undo that). Resets only on
    // process restart, same as m_profile itself effectively would.
    bool m_killBoardLoggedOut{false};
    // Entered when a GoRoom lands on target 11. Seeds m_pilotStats (see the
    // reference starting values in the terminal-screens implementation
    // plan) the first time ever there's no save-derived data yet.
    void enterKillBoardRoom();
    // Self-contained render+click, same reasoning/shape as
    // renderDutyLogsPage() — the CALLSIGN/ACES/KILLS table isn't gump-zones.
    void renderKillBoardPage();

    // Loadout (room 0010) — the player's OWN weapons loadout screen (not
    // wingmen — the player can only configure their own craft). Backed by
    // the real data model parsed from GAMEFLOW/LOADOUT.IFF (see
    // WC3LoadoutData.h/.cpp) — craft list, per-hardpoint capacity/weapon-
    // compatibility mask, and real screen-space hardpoint marker positions
    // all come directly from that file, not reconstructed/guessed.
    // Interaction, confirmed against wc3_008.png-wc3_011.png:
    //   "previous"/"next"      — cycle craft (m_loadoutDb.craft, in file
    //                            order == 183.SHP frame order; several
    //                            consecutive entries are all "EXCALIBUR"
    //                            with genuinely different hardpoint sets).
    //   top ◄/►                — cycle which hardpoint (weapon bank) on the
    //                            current craft is selected.
    //   bottom ◄/►             — cycle the missile type loaded in the
    //                            selected hardpoint, restricted to
    //                            m_loadoutDb.compatibleWeapons(hp) — a
    //                            hardpoint whose mask matches only one
    //                            weapon (e.g. Excalibur's Temblor Bomb/
    //                            Cloaking Device bays) has nothing to
    //                            cycle to, with no separate "special
    //                            hardpoint" flag needed anywhere.
    //   "+"/"-"                — add/remove one unit from the selected
    //                            hardpoint's loaded count, clamped to
    //                            [0, capacity].
    //   "proceed"              — commits (the in-memory state below already
    //                            *is* the committed loadout — nothing else
    //                            to apply yet, since no mission-launch
    //                            weapon-application system exists) and
    //                            returns to Flight Control.
    // True while the current room is the Loadout screen (room id "0010").
    bool m_inLoadout{false};
    WC3LoadoutDatabase m_loadoutDb;
    void ensureLoadoutDataLoaded();
    int m_loadoutShipIndex{0};
    int m_loadoutSelectedHardpoint{0};
    // Alt+S toggles the SUBS spec-sheet text (see WC3LoadoutData.h) drawn
    // underneath the craft name in renderLoadoutPage().
    bool m_loadoutShowSubs{false};
    // Sized to the selected craft's own hardpoint count; reset to that
    // craft's defaults (see resetLoadoutHardpoints()) whenever the craft
    // changes, since only the currently-selected craft's loadout is ever
    // tracked/persisted at once — matches WC3SaveGame's LOAD chunk shape.
    std::vector<WC3LoadoutHardpointState> m_loadoutHardpoints;
    // (Re)initializes m_loadoutHardpoints to the given craft's defaults —
    // every hardpoint filled to capacity with its first compatible weapon.
    // Called on entry and whenever "previous"/"next" changes the selected
    // craft.
    void resetLoadoutHardpoints(int shipIndex);
    // Entered when a GoRoom lands on target 10.
    void enterLoadoutRoom();
    // Self-contained render+click, same shape as renderDutyLogsPage()/
    // renderKillBoardPage() — the ship-render/hardpoint panels aren't
    // gump-zones either.
    void renderLoadoutPage();
    // Draws the Login page's boot-style text + live callsign field into the
    // room's type=515 text-input gump's own rect, via displaySceneFramebuffer's
    // drawExtra hook. Called from runFrame() while m_inTerminal &&
    // m_terminalPage==Login.
    void renderLoginPage();

    // Pending state while AWAITING_BRANCH_CHOICE: which BRANCH pak/entry the player
    // is choosing between, and the rest of the click's action to run once resolved.
    int m_pendingBranchPak{-1};
    int m_pendingBranchEntry{-1};
    WC3Scene::ActionResult m_pendingGumpAction{};
    // The gump's own movie (already played as the intro shot before the choice
    // prompt appeared) — kept separately since m_pendingGumpAction.movieName is
    // cleared to stop finishGumpAction() replaying it. Used to play the correct
    // TRUE/FALSE reaction shot (branchTrueShot/branchFalseShot) once resolved.
    std::string m_pendingBranchMovieName;
    // Keyboard highlight state while AWAITING_BRANCH_CHOICE: -1 = nothing
    // highlighted yet, 0 = TRUE (top) line highlighted, 1 = FALSE (bottom) line
    // highlighted. Up/Down move this without committing; Enter commits it.
    int m_branchChoiceSelected{-1};

    RSVGA& VGA = RSVGA::getInstance();
    AssetManager& Assets = AssetManager::getInstance();
    RSMixer& Mixer = RSMixer::getInstance();
    Keyboard* m_keyboard{nullptr};

    void playMovie(const char* name);
    void loadScene(int sceneId);
    void loadRoomPalette(int roomIndex);
    // independentFrames: WC3 shapes with sub-1 sized deltas are normally
    // decoded as a running accumulation (frame N = frame N-1 patched by
    // sub N's own RLE stream) — correct for animations like door-open
    // sequences, whose later frames only encode what changed. The Loadout
    // screen's ship/weapon *selector* shapes (183/184/185.SHP) instead pack
    // several completely unrelated, independently-complete images into one
    // shape (e.g. 183.SHP frame 1 is the Hellcat V, not "the Arrow plus a
    // delta") — accumulating them bled the previous selection's leftover
    // pixels through wherever the new frame's own art doesn't fully cover,
    // which is exactly the "switching ships shows multiple ships at once"
    // bug this parameter fixes. Pass true only for such gallery-style
    // shapes; every other call site keeps the default (false) delta
    // behavior doors etc. depend on.
    RSImageSet* getShape(int shapeId, bool independentFrames = false);
    std::unordered_map<int, RSImageSet*> shapeCache;
    std::unordered_map<int, RSImageSet*> shapeCacheIndependent;

    // SHAPES.PAK is structured as repeated [PAL(768 bytes), SHP, SHP, ...]
    // groups — one room (or terminal interface)'s own palette followed by
    // every shape entry that belongs to it, up to the next PAL entry. A
    // shape's *correct* palette is whichever PAL immediately precedes it in
    // this sequence — normally that's the same as the current room's own
    // palette_id (its shapes live in its own group), but a handful of gumps
    // reference a shapeId that falls in a *different* group (confirmed:
    // room 0003's gump 18 -> shapeId 188, room 0008's gump 54 -> shapeId
    // 189, both actually belonging to room 0014's group/PAL 177) — these
    // were rendering with completely wrong colors since getShape() only
    // ever decoded raw palette indices, with no way to reconcile "this
    // shape's indices assume a different 256-color palette than the one
    // currently loaded into VGA." Since the engine resolves indices to RGB
    // through exactly one active palette at final display time (no per-
    // shape palette switch), out-of-group shapes are remapped once at
    // decode time instead: each of their pixel indices is looked up in
    // their own correct palette, then substituted with whichever index in
    // the *current* room's palette has the closest RGB — a lossy but
    // visually-correct-enough one-time requantization, needing no changes
    // to the shared render/display pipeline.
    std::vector<int> m_palIndices; // sorted PAL-entry indices within shapePak, built lazily
    void ensurePalIndices();
    // Nearest preceding PAL-entry index owning shapeId, or -1 if shapeId is
    // before the first PAL entry (shouldn't happen for real shape data).
    int findOwningPaletteId(int shapeId);
    // Builds a 256-entry lookup table mapping each index under fromPaletteId
    // to the closest-RGB index under toPaletteId (by squared distance); 0xFF
    // (transparent) always maps to itself. Cached per (from,to) pair.
    std::unordered_map<uint64_t, std::array<uint8_t, 256>> m_paletteRemapCache;
    const std::array<uint8_t, 256>& getPaletteRemap(int fromPaletteId, int toPaletteId);

    GLuint sceneTexture{0};
    RLEShape* m_cursor{nullptr};
    bool m_cursor_owned{false};  // false when shape is owned by WC3Globals
    std::string m_hoverLabel;       // label of the zone under the cursor this frame
    uint32_t    m_hoveredGumpId{0}; // gump_id of zone under cursor (0 = none)
    uint32_t    m_hoveredZoneType{0};
    // Disambiguates TYPE_NAVPOINT zones for cursor selection — see
    // WC3Scene::Zone::isRoomChange.
    bool        m_hoveredIsRoomChange{false};
    // Tracks which zone is focused for keyboard/right-click cycling (Tab or
    // right-click advances it; -1 = none). Enter activates the focused zone.
    int         m_focusZoneIdx{-1};
    void initSceneFramebuffer();
    // darken: multiplies scene RGB before drawing (1.0 = unchanged; dialogs use
    // ~0.4 so overlay text/UI stands out against the frozen scene behind it).
    // drawHoverLabel: the normal SCENE_ACTIVE hover-label text; dialogs pass
    // false since m_hoverLabel belongs to that state, not theirs.
    // drawExtra: called with the RGBA buffer (pre-flip, y-down) after the
    // palette conversion/darken/hover-label, for dialog-specific text/UI —
    // reused by every overlay page (branch-choice prompt, quit confirm,
    // simulator list, and the new terminal/killboard/loadout pages) instead
    // of each duplicating the render/convert/flip/glDrawPixels boilerplate.
    void displaySceneFramebuffer(float darken = 1.0f, bool drawHoverLabel = true,
                                  const std::function<void(uint32_t*, int, int)>& drawExtra = nullptr);
    void drawCursor(int vx, int vy);
    // Blits one frame of an arbitrary SHAPES.PAK shape straight into sceneFB
    // (palette-index space), same mechanism WC3Scene::render() uses for room
    // actors — for shapes that aren't part of the current room's own actor
    // list (e.g. the Loadout screen's 183/184/185.SHP ship/weapon art).
    // Must be called before displaySceneFramebuffer()'s palette->RGBA pass,
    // and relies on the room's own palette already being active in VGA
    // (true for room 0010, whose own palette these shapes were authored
    // against — confirmed by headless decode, not assumed).
    void drawShapeIntoScene(int shapeId, int frame, int x, int y);
    // Fills a rect of sceneFB with a flat palette index (255 = the
    // transparent/background key, same as sceneFB->clear()) — used to wipe a
    // panel clean immediately before redrawing it with a different craft's/
    // weapon's sprite, so switching selections can never leave a trace of
    // the previous one behind underneath the new one.
    void fillSceneRect(int x, int y, int w, int h, uint8_t colorIndex = 255);

    // Advance m_focusZoneIdx to the next zone in the current room and warp the
    // mouse cursor to its centre (shared by Tab and right-click).
    void cycleZoneFocus();
    // Run the ACTV action for a clickable zone (shared by mouse-click and
    // keyboard activation of the focused zone).
    void activateZone(int zoneIdx);
    // Runs a gump's own ACTV code by id and processes the result — shared by
    // activateZone() (a real click) and SCENE_ACTIVE's per-frame poll of
    // scene->pollFinishedAnimation() (a VAR(44) animation completing on its own,
    // e.g. each link of the elevator's travel-animation chain).
    // isActorConversation: true only when activateZone() knows this gump is
    // a TYPE_CHARACTER/TYPE_CHARACTER2 "talk to X" portrait, not a door/
    // animation-chain link — see m_talkedToActorsThisVisit's own comment.
    void runGumpAction(uint32_t gumpId, bool isActorConversation = false);
};

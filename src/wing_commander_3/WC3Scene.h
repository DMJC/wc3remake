#pragma once
#include "../strike_commander/precomp.h"

// A GUMP is an interactive UI element in a WC3 scene room
struct WC3Gump {
    uint32_t id{0};
    uint32_t type{0};
    int scrn_id{-1};
    int x{0};
    int y{0};
    int w{0};   // bounding-box width  (from 4-value CORD: x2-x)
    int h{0};   // bounding-box height (from 4-value CORD: y2-y)
    int titl_id{-1};
    int pntr_id{-1};
    int anim_fps{0};
    std::vector<uint8_t> code;
    // Only populated for TYPE_MENU_ACTION (505): inline ENG action strings
    std::vector<std::string> menu_items;
    // Sample indices from this gump's own FORM SOND/_ID_ chunk (GAMEFLOW/
    // SOUND/GFSAMPLE.IFF record indices). Empty = no attached sound. One
    // entry = played once on hover-start; two entries = [0] hover-start
    // (e.g. door open), [1] hover-end (e.g. door close) — observed pattern:
    // door-type gumps carry a matching-length open/close sample pair.
    std::vector<int> sound_ids;

    // TYPE values from ACTR / XTRA GUMP blocks
    // TYPE_MOVIE_BG/BG2: despite the name (an early guess), these are ordinary
    // decorative overlay sprites layered over a full-screen background plate —
    // static (1 frame) or auto-looping — not FMV placeholders; render normally.
    static constexpr uint32_t TYPE_MOVIE_BG  = 1;
    static constexpr uint32_t TYPE_MOVIE_BG2 = 2;
    static constexpr uint32_t TYPE_BUTTON    = 3;    // clickable sprite-button (e.g. lift floor selector)
    static constexpr uint32_t TYPE_LOOP_BG   = 4;    // auto-looping background sprite (e.g. gunnery control readouts)
    static constexpr uint32_t TYPE_PERSON    = 10;   // hover-only animated element (e.g. a door that plays on hover)
    static constexpr uint32_t TYPE_CHARACTER = 0x0B; // interactive character, hover-only animated
    static constexpr uint32_t TYPE_MOVIE_FG  = 5;    // auto-looping foreground/background sprite (shuttle, readouts, etc.)
    static constexpr uint32_t TYPE_NAVPOINT  = 500;  // invisible navigation hotspot (CORD = bounding rect)
    static constexpr uint32_t TYPE_SOUND     = 502;  // ambient sound
    static constexpr uint32_t TYPE_TRIGGER   = 504;  // script trigger
    static constexpr uint32_t TYPE_MENU      = 0x1F9;
    // One-shot triggered animation (e.g. elevator travel/door clips) — never
    // clickable, hidden by default, only shown for the duration of a VAR(44)-
    // triggered playback kicked off by another gump's ACTV code.
    static constexpr uint32_t TYPE_ANIM       = 6;
    // Another "talk to <wingman>" character gump, same shape/role as
    // TYPE_CHARACTER (interactive, hover-only animated, id in the 5000+
    // roster-portrait range) — confirmed via VICTORY.IFF's mess hall under
    // SCEN0001 (Vaquero's own portrait gump, whose titl_id matches its
    // room's "Talk to VAQUERO." menu item exactly like an ordinary
    // TYPE_CHARACTER entry would). Exact distinction from TYPE_CHARACTER
    // itself unconfirmed — was previously unhandled by buildZones() at
    // all, so gumps of this type rendered but had no clickable zone.
    static constexpr uint32_t TYPE_CHARACTER2 = 516;
    // Loadout Terminal (room 0008) gump types — confirmed via a headless
    // decode of its own SHAPES.PAK art (checkbox sprite is a plain on/off
    // square, no baked-in label text; the text-input region and content-box
    // regions carry no sprite at all, just CORD bounding rects read directly
    // by WC3GameFlow to lay out engine-drawn text/lists).
    static constexpr uint32_t TYPE_CHECKBOX    = 512; // on/off toggle sprite (Options screen)
    static constexpr uint32_t TYPE_TEXT_INPUT  = 515; // bounding-rect only — Login's callsign entry box
    // Bounding-rect-only content areas with no sprite of their own — behave
    // identically today regardless of which raw type number a given gump
    // uses: the Options checkbox grid (510), Kill Board's CALLSIGN/ACES/KILLS
    // table (511), Loadout's ship-portrait/weapon-list panels (508/509), and
    // a few more seen carrying no sprite (501/507/513). Real IFF data uses
    // whichever of these numbers the room happens to; isContentBox() below
    // is the single place that treats them uniformly.
    static bool isContentBox(uint32_t type) {
        switch (type) {
            case 501: case 507: case 508: case 509:
            case 510: case 511: case 513:
                return true;
            default:
                return false;
        }
    }
};

// A room within a stage (VICTORY.IFF / STGE).
// After loadScene(), XTRA gumps and ACTV codes from the SCEN file are merged in.
struct WC3StageRoom {
    std::string id;
    int palette_id{-1};
    int bkgd_shape_id{-1};
    int bkgd_x{0};
    int bkgd_y{0};
    std::vector<WC3Gump> actors;          // stage ACTR + scene XTRA (merged)
    std::vector<uint8_t> init_code;
    std::vector<uint8_t> exit_code;
    // Per-gump activation scripts — scene ACTV overrides stage ACTV when present
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> actv_codes;
    // English action-menu items from the TYPE_MENU_ACTION (505) gump, e.g. "Go to LIFT."
    std::vector<std::string> menu_items;

    // Runtime VM state, reset and re-derived from INIT each time the room is entered
    // (see WC3Scene::runRoomInit). Explicit per-gump visibility override (VAR(55)/VAR(56));
    // absent = use the type-based default (TYPE_ANIM defaults hidden, everything else visible).
    mutable std::unordered_map<uint32_t, bool> gumpVisible;
    // gumpId → SDL tick when its VAR(44)-triggered one-shot animation finishes.
    // Polled by WC3GameFlow each frame; once passed, that gump's own ACTV code is
    // fired once (as if clicked) and the entry is removed.
    mutable std::unordered_map<uint32_t, uint32_t> animEndTick;
    // gumpId → SDL tick when its currently-playing triggered animation started
    // (frame = elapsed / frameDurationMs), used by render() for one-shot playback.
    mutable std::unordered_map<uint32_t, uint32_t> animStartTick;
    // gumpId → forced frame index, checked by render() before any of the
    // triggered/hover/auto-loop logic above. Used by callers that need a
    // gump to hold a specific frame indefinitely (e.g. the lift's floor-
    // select buttons staying lit on the currently-selected floor) rather
    // than the one-shot "play then revert/hold" semantics animStartTick
    // gives. Reset on room (re-)entry, same as the other runtime maps.
    mutable std::unordered_map<uint32_t, int> pinnedFrame;
    // TYPE_PERSON (door-style hover) animation state. These sprites pack two
    // independent one-shot sequences in one frame set — the first half is
    // the opening animation, the second half is closing (not a single
    // forward/reverse cycle across all frames). doorOpening: gumpId ->
    // which sequence last started (true = opening, playing/holding within
    // frames [0, n/2-1); false = closing, within [n/2, n-1]). doorAnimTick:
    // the tick that sequence started, so render() can compute elapsed
    // frames without looping. Absent entries mean "never interacted with
    // yet" — held at the resting closed frame (0).
    mutable std::unordered_map<uint32_t, bool> doorOpening;
    mutable std::unordered_map<uint32_t, uint32_t> doorAnimTick;
};

struct WC3StageData {
    std::vector<uint8_t> init_code;
    std::vector<WC3StageRoom> rooms;
};

class WC3Scene {
public:
    WC3Scene();
    ~WC3Scene();

    bool loadStage(const uint8_t* data, size_t size);

    // Parse a SCEN file and merge its XTRA/ACTV data into the already-loaded stage.
    // Returns the stage name referenced by the SCEN (caller uses this to load the right IFF).
    bool loadScene(const uint8_t* data, size_t size);

    // Extract just the STGE name from raw SCEN bytes (call before loadStage to know which IFF to load).
    static std::string extractStageName(const uint8_t* data, size_t size);

    void setRoom(int roomIndex, std::function<RSImageSet*(int)> getShape = nullptr);
    // hoveredGumpId: the gump_id of the zone the cursor is currently over (-1 = none).
    // Used to drive hover-only animations (TYPE_CHARACTER).
    // yOffset: added to every drawn actor/background's y position, default 0
    // (no change). The Loadout/Options/Terminal/Kill Board "computer
    // console" screens render ~23px higher than authentic DOSBox — a fixed
    // offset confirmed by comparing real actor positions against reference
    // screenshots (two independent elements, a static background and a
    // real button, both off by exactly 23px regardless of screen depth,
    // ruling out a scale error). The room's own CORD data is unmodified;
    // WC3GameFlow passes a non-zero yOffset only while rendering those
    // specific screens, leaving ordinary rooms (flight deck, mess hall,
    // etc.) untouched since they haven't been confirmed to have the same
    // discrepancy.
    void render(FrameBuffer* fb, PakArchive* shapes,
                std::function<RSImageSet*(int)> getShape, uint32_t hoveredGumpId = 0,
                int yOffset = 0);

    // A movie to play as a side effect of running INIT/EXIT bytecode (room entry/exit
    // setup, as opposed to a player-clicked gump's own ActionResult.movieName).
    struct PendingMovie { std::string name; int shot{0}; };

    // Runs a room's own INIT bytecode: resets that room's gump-visibility state to
    // type-based defaults, then executes INIT (VAR(8)/(50)/(57) SET, VAR(55)/(56)
    // show/hide, VAR(44) trigger-animation, VAR(45) movie-select). Call once right
    // after setRoom(). Returns any movies the INIT code wants played, in order.
    std::vector<PendingMovie> runRoomInit(int roomIndex, std::function<RSImageSet*(int)> getShape = nullptr);
    // Runs a room's own EXIT bytecode (e.g. VAR(8) 4 N records the departing room
    // number). Call right before switching to a new room via setRoom().
    std::vector<PendingMovie> runRoomExit(int roomIndex, std::function<RSImageSet*(int)> getShape = nullptr);

    // Polls the current room's VAR(44)-triggered one-shot animations for completion
    // (SDL_GetTicks() past their end tick). Returns the gump_id of the first gump
    // whose animation just finished (removing its pending-completion entry so it
    // only fires once), or 0 if none. Caller should treat this exactly like a click:
    // activateGump(id) then process the ActionResult.
    uint32_t pollFinishedAnimation();

    // Directly triggers a gump's one-shot VAR(44) animation (dark/inactive ->
    // lit/active frame sequence, held on the last frame — see render()'s
    // animation policy) in the current room, without going through that
    // gump's own ACTV bytecode. Used by callers that already know exactly
    // which gump to animate but can't rely on the gump's own ACTV code to
    // resolve it (e.g. the Simulator terminal's buttons, whose ACTV bytecode
    // only carries an unresolved/ambiguous OP44 operand — see
    // State::SIMULATOR's comment in WC3GameFlow.h).
    // armCompletionPoll: when true (default), also arms animEndTick, so a
    // caller polling pollFinishedAnimation() (e.g. SCENE_ACTIVE's per-frame
    // poll) will pick up this gump's completion and auto-fire its own ACTV
    // code once the animation finishes — the intended behaviour for real
    // chained-animation links (e.g. an elevator door segment triggering the
    // next). Pass false for a caller that wants purely the visual one-shot
    // playback (animStartTick) without that gump's ACTV code ever firing as
    // a side effect — e.g. the lift's travel-indicator gumps, whose real
    // ACTV bytecode implements a from-scratch floor-selection mechanism
    // this engine reimplements directly instead (see WC3GameFlow's lift
    // handling) rather than relying on.
    void triggerGumpAnim(uint32_t gumpId, std::function<RSImageSet*(int)> getShape = nullptr,
                          bool armCompletionPoll = true);

    // Forces a gump to render at a specific frame indefinitely, bypassing
    // triggerGumpAnim/animStartTick and auto-loop entirely — see
    // WC3StageRoom::pinnedFrame. Pass frame < 0 to unpin (fall back to the
    // normal animation policy).
    void setGumpPinnedFrame(uint32_t gumpId, int frame);
    // Directly sets a gump's visibility in the current room, the same as
    // the ACTV interpreter's own VAR(55)/(56) would. Used by callers that
    // drive a room's gumps from outside its ACTV bytecode entirely (e.g.
    // the lift's floor buttons — see WC3GameFlow's lift-handling code).
    void setGumpVisible(uint32_t gumpId, bool visible);
    // Rebuilds the clickable zone list from each actor's *current*
    // gumpVisible state. runRoomInit() already does this once on its own,
    // right after INIT bytecode runs (see its own comment) — callers that
    // change a gump's visibility afterward (e.g. WC3GameFlow reapplying a
    // persisted actor-removal right after runRoomInit()) need to call this
    // again themselves, or the old zone stays hoverable/clickable even
    // though the actor stopped rendering.
    void buildZones(std::function<RSImageSet*(int)> getShape);

    int getCurrentRoom() const { return currentRoom; }
    const WC3StageData& getStageData() const { return stageData; }
    int getRoomCount() const { return static_cast<int>(stageData.rooms.size()); }

    // Clickable zones for current room
    struct Zone {
        int x, y, w, h;
        uint32_t gump_id;
        uint32_t type{0};   // actor TYPE value — used for cursor selection
        std::string label;
        int titl_id{-1};
        int anim_fps{0};
        // TYPE_NAVPOINT covers both real room-to-room navigation ("Go to
        // LIFT.") and other actions like "Activate MAIN TERMINAL." — both
        // share type==TYPE_NAVPOINT, so the gump type alone can't tell a
        // cursor whether to show the room-change (cross) or activate
        // (computer-screen) icon. Set from a dry-run activateGump() in
        // buildZones(): true only when that action is ActionResult::GoRoom.
        bool isRoomChange{false};
    };
    const std::vector<Zone>& getZones() const { return zones; }
    int hitTest(int mx, int my) const;

    // Actor gump IDs in the current room that carry sound_ids and are
    // effectively "hovered" this frame, under the same hover-only +
    // spatial-overlap policy render() uses to drive hover-only animations
    // (TYPE_CHARACTER/TYPE_PERSON, exact gump_id match OR bounding-box
    // overlap with the hovered zone). Caller diffs this against the previous
    // frame's result to detect hover-start/hover-end transitions and trigger
    // the attached sound_ids.
    std::vector<uint32_t> getHoveredActorIds(uint32_t hoveredGumpId,
                                              std::function<RSImageSet*(int)> getShape) const;
    // Looks up a gump's sound_ids by id within the current room, or nullptr
    // if the gump has none / isn't found.
    const std::vector<int>* getActorSoundIds(uint32_t gumpId) const;

    // Decode and execute the ACTV script for a gump.
    // VAR(6) N  → GoRoom:  navigate to SCEN room index N (maps via room ID "%04d")
    // VAR(6) N  → GoRoom:  navigate to SCEN room index N
    // VAR(7) N  → subscene N (N≤18: SC_205 shot; N>18: dialogue subscene, not a SCEN switch)
    struct ActionResult {
        enum Type { None, GoRoom } type = None;
        int target{-1};
        // SC_205.MVE shot index to play before the action (-1 = none, 0 = default walk).
        // Ignored when movieName is set — that takes priority.
        int transitionShot{-1};
        // Dialogue subscene to play (VAR(7) N where N > 18), -1 = none.
        int subscene{-1};
        // BRANCH PAK entry index to play (Blair voice line, -1 = none).
        int branchEntry{-1};
        // Explicit movie (e.g. "sc_6") + shot from a VAR(45) LOOKMOVI-index call in the
        // gump's own ACTV code, optionally followed by a VAR(47) shot number. Empty =
        // no override — caller falls back to transitionShot (SC_205.MVE).
        std::string movieName;
        int movieShot{0};
        // Full shot sequence when VAR(47) names more than one shot for
        // movieName (the deferred/conditional form — e.g. a wingman's own
        // reaction movie: "IF(bankA[12]) shots=[0,2] ELSE shots=[1,2]", win
        // or loss opener followed by a shared closer). Empty for the common
        // single-shot case (movieShot alone is enough then). When non-empty,
        // callers should play every entry in order instead of just
        // movieShot — movieShot is still set to shots[0] for callers that
        // haven't been updated to look at this list.
        std::vector<int> movieShots;
        // When branchEntry >= 0 and the ACTV code immediately follows VAR(49) with an
        // "if GET(bank-A slot)" whose two branches each open with VAR(47) <shot>, these
        // are that shot pair — the reaction clip of `movieName` to play after the player
        // picks the TRUE or FALSE branch line respectively. -1 = no such pattern found
        // (nothing to play after the choice beyond the branch-pak audio itself).
        int branchTrueShot{-1};
        int branchFalseShot{-1};
        // OP1 (no operand): "commit to flying" signal found at the end of the
        // ready-room's post-wingman-selection "Go to FLIGHT DECK." door
        // variant. Caller (WC3GameFlow) should hand off to mission flight.
        bool launchMission{false};
        // OP5 N: LOOKMISN.IFF index of the mission this playthrough is about
        // to fly, set when talking to Flight Control's crew (the gump that
        // leads into wingman selection). -1 = none. Confirmed via LOOKMISN.IFF:
        // walking every SCEN's own OP5 argument matches its own scene number
        // almost exactly (SCEN0000 -> OP5 0 -> LOOKMISN[0] = "misna001", the
        // handful of exceptions are real campaign branches, e.g. SCEN0036's
        // OP5 61 -> LOOKMISN[61] = "misnl01d").
        int missionIndex{-1};
    };
    // getShape is used only to size a VAR(44)-triggered one-shot animation (frame
    // count ÷ fps); pass nullptr to skip that (falls back to a fixed duration).
    // dryRun suppresses state mutation (VAR(8)/(50)/(55)/(56)/(57)/(44)) while still
    // computing the returned ActionResult — used by buildZones() to test whether a
    // gump has a live action (for hover labeling) without side-effecting the room.
    ActionResult activateGump(uint32_t gump_id, std::function<RSImageSet*(int)> getShape = nullptr,
                               bool dryRun = false) const;

    // English action-menu strings for the current room (from TYPE_MENU_ACTION gump).
    const std::vector<std::string>& getRoomMenuItems() const;


    // Return the stageData room index whose id == "%04d" % scenRoomIndex, or -1.
    int scenRoomToStageIndex(int scenRoomIndex) const;

    // LOOKMOVI.IFF's index → movie-basename table (e.g. 24 → "sc_6"), used to resolve
    // VAR(45) calls in ACTV bytecode. Set once at startup from WC3GameFlow.
    void setLookMovi(std::unordered_map<int, std::string> table) { lookMovi = std::move(table); }

    // Directly seeds a scene-global bank-B variable (see varsA/varsB below).
    // Used by WC3GameFlow to approximate narrative-progression flags that the
    // real game sets as a side effect of subscene (dialogue/briefing) content
    // we don't yet play back — e.g. bank-B slot 155 ("wingman selection
    // committed", read by Flight Control's ready-room INIT/door-swap logic)
    // and the per-pilot roster-availability flags 143-149 read by the
    // wingman-selection room's own INIT.
    void setBankB(int slot, int32_t value) { varsB[slot] = value; }
    // Same, for bank-A. Confirmed use: slot 12 is "was the just-completed
    // mission successful" — multiple different wingman-portrait gumps
    // (e.g. Hobbes at Flight Control, a different pilot in the Barracks)
    // each independently select between two shot-lists of their own
    // reaction movie via an identical "IF(bankA[12]) {success shots} ELSE
    // {failure shots}" pattern. Nothing else in the interpreter ever
    // writes bank-A, so without this every reaction always plays its
    // failure branch regardless of actual mission outcome.
    void setBankA(int slot, int32_t value) { varsA[slot] = value; }
    // Reads a scene-global bank-A slot, defaulting to 0 if never set — same
    // default GET(12) itself uses in bytecode. Used by callers that need to
    // read back a flag set as a side effect of room EXIT (e.g. bank-A slot
    // 4, "the room the player just departed from," read on entering the
    // lift to seed which floor it should start on).
    int32_t getBankA(int slot) const {
        auto it = varsA.find(slot);
        return it != varsA.end() ? it->second : 0;
    }
    // Same, for bank-B — e.g. reading back the roster-availability flags
    // 143-149 (see setBankB's comment) to know which pilots are actually
    // selectable in the Loadout screen right now.
    int32_t getBankB(int slot) const {
        auto it = varsB.find(slot);
        return it != varsB.end() ? it->second : 0;
    }
    // Read-only bulk access for save-game serialization (WC3SaveGame) — the
    // whole varsA/varsB maps, not just one slot. Most entries (e.g. the
    // roster-availability flags 143-149) are deterministically re-derived by
    // loadScene() itself and don't need saving; the save format only dumps
    // a small allowlist of slots confirmed not to be — see WC3SaveGame.cpp.
    const std::unordered_map<int, int32_t>& getAllBankA() const { return varsA; }
    const std::unordered_map<int, int32_t>& getAllBankB() const { return varsB; }

private:
    WC3StageData stageData;
    int currentRoom{0};
    std::vector<Zone> zones;
    std::unordered_map<int, std::string> lookMovi;

    // Scene-global VM variable banks (VAR(11)="bank B", VAR(12)="bank A"), persist
    // across room changes — e.g. the elevator's "Go to LIFT." gumps set bank-A slot 4
    // to their own room number before navigating, and the lift room's INIT later
    // reads it back to know which floor the player came from.
    mutable std::unordered_map<int, int32_t> varsA;
    mutable std::unordered_map<int, int32_t> varsB;

    static uint32_t readU32LE(const uint8_t* p);
    static uint32_t readU32BE(const uint8_t* p);

    void parseGumps(const uint8_t* data, size_t size, std::vector<WC3Gump>& out,
                    bool skipMenu = false);
    void parseGump(const uint8_t* data, size_t size, WC3Gump& gump);

    // true if gumpId is currently visible in `room` (explicit VAR(55)/(56) override,
    // else the type-based default: TYPE_ANIM defaults hidden, everything else visible).
    bool isGumpVisible(const WC3StageRoom& room, uint32_t gumpId, uint32_t type) const;

    // Shared bytecode interpreter core. Executes `code` linearly with a small value
    // stack and structured if/else/endif control flow (see .cpp for the full grammar
    // derived from static analysis of the game's own GAMEFLOW scripts). `room` is the
    // room whose gumpVisible/animEndTick/animStartTick state VAR(44)/(55)/(56) affect.
    // `allowNav` gates whether VAR(6)/(7)/(49)/(45) populate `result` (true only for a
    // clicked gump's own ACTV; INIT/EXIT code ignores room-nav/branch/dialogue opcodes
    // but still collects VAR(45) movie-macros into `movies`). `dryRun` suppresses all
    // state mutation (variable SET, gump show/hide, animation trigger) while still
    // computing `result`/`movies` — see activateGump()'s dryRun for why this exists.
    void runBytecode(const std::vector<uint8_t>& code, const WC3StageRoom& room,
                      bool allowNav, ActionResult* result,
                      std::vector<PendingMovie>* movies,
                      std::function<RSImageSet*(int)> getShape,
                      bool dryRun = false) const;
};

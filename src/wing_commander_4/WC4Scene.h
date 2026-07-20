#pragma once
#include "../strike_commander/precomp.h"

// A GUMP is an interactive UI element in a WC4 scene room
struct WC4Gump {
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

    // TYPE values from ACTR / XTRA GUMP blocks
    static constexpr uint32_t TYPE_MOVIE_BG  = 1;    // background video element (crew)
    static constexpr uint32_t TYPE_MOVIE_BG2 = 2;    // background video element (alt)
    static constexpr uint32_t TYPE_BUTTON    = 3;    // clickable sprite-button (e.g. lift floor selector)
    static constexpr uint32_t TYPE_PERSON    = 10;   // person/pilot (no video)
    static constexpr uint32_t TYPE_CHARACTER = 0x0B; // interactive character
    static constexpr uint32_t TYPE_MOVIE_FG  = 5;    // foreground object (shuttle etc.)
    static constexpr uint32_t TYPE_NAVPOINT  = 500;  // invisible navigation hotspot (CORD = bounding rect)
    static constexpr uint32_t TYPE_SOUND     = 502;  // ambient sound
    static constexpr uint32_t TYPE_TRIGGER   = 504;  // script trigger
    static constexpr uint32_t TYPE_MENU      = 0x1F9;
};

// A room within a stage (VICTORY.IFF / STGE).
// After loadScene(), XTRA gumps and ACTV codes from the SCEN file are merged in.
struct WC4StageRoom {
    std::string id;
    int palette_id{-1};
    int bkgd_shape_id{-1};
    int bkgd_x{0};
    int bkgd_y{0};
    std::vector<WC4Gump> actors;          // stage ACTR + scene XTRA (merged)
    std::vector<uint8_t> init_code;
    std::vector<uint8_t> exit_code;
    // Per-gump activation scripts — scene ACTV overrides stage ACTV when present
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> actv_codes;
    // English action-menu items from the TYPE_MENU_ACTION (505) gump, e.g. "Go to LIFT."
    std::vector<std::string> menu_items;
};

struct WC4StageData {
    std::vector<uint8_t> init_code;
    std::vector<WC4StageRoom> rooms;
};

class WC4Scene {
public:
    WC4Scene();
    ~WC4Scene();

    bool loadStage(const uint8_t* data, size_t size);

    // Parse a SCEN file and merge its XTRA/ACTV data into the already-loaded stage.
    // Returns the stage name referenced by the SCEN (caller uses this to load the right IFF).
    bool loadScene(const uint8_t* data, size_t size);

    // Extract just the STGE name from raw SCEN bytes (call before loadStage to know which IFF to load).
    static std::string extractStageName(const uint8_t* data, size_t size);

    void setRoom(int roomIndex, std::function<RSImageSet*(int)> getShape = nullptr);
    // hoveredGumpId: the gump_id of the zone the cursor is currently over (-1 = none).
    // Used to drive hover-only animations (TYPE_CHARACTER).
    void render(FrameBuffer* fb, PakArchive* shapes,
                std::function<RSImageSet*(int)> getShape, uint32_t hoveredGumpId = 0);

    int getCurrentRoom() const { return currentRoom; }
    const WC4StageData& getStageData() const { return stageData; }
    int getRoomCount() const { return static_cast<int>(stageData.rooms.size()); }

    // Clickable zones for current room
    struct Zone {
        int x, y, w, h;
        uint32_t gump_id;
        uint32_t type{0};   // actor TYPE value — used for cursor selection
        std::string label;
        int titl_id{-1};
        int anim_fps{0};
    };
    const std::vector<Zone>& getZones() const { return zones; }
    int hitTest(int mx, int my) const;

    // Decode and execute the ACTV script for a gump.
    // VAR(6) N  → GoRoom:  navigate to SCEN room index N (maps via room ID "%04d")
    // VAR(6) N  → GoRoom:  navigate to SCEN room index N
    // VAR(7) N  → subscene N (N≤18: SC_205 shot; N>18: dialogue subscene, not a SCEN switch)
    struct ActionResult {
        enum Type { None, GoRoom } type = None;
        int target{-1};
        // SC_205.MVE shot index to play before the action (-1 = none, 0 = default walk).
        int transitionShot{-1};
        // Dialogue subscene to play (VAR(7) N where N > 18), -1 = none.
        int subscene{-1};
        // BRANCH PAK entry index to play (Blair voice line, -1 = none).
        int branchEntry{-1};
    };
    ActionResult activateGump(uint32_t gump_id) const;

    // English action-menu strings for the current room (from TYPE_MENU_ACTION gump).
    const std::vector<std::string>& getRoomMenuItems() const;


    // Return the stageData room index whose id == "%04d" % scenRoomIndex, or -1.
    int scenRoomToStageIndex(int scenRoomIndex) const;

private:
    WC4StageData stageData;
    int currentRoom{0};
    std::vector<Zone> zones;

    static uint32_t readU32LE(const uint8_t* p);
    static uint32_t readU32BE(const uint8_t* p);

    void parseGumps(const uint8_t* data, size_t size, std::vector<WC4Gump>& out,
                    bool skipMenu = false);
    void parseGump(const uint8_t* data, size_t size, WC4Gump& gump);
    void buildZones(std::function<RSImageSet*(int)> getShape);
};

#pragma once
#include "../strike_commander/precomp.h"
#include "WC4Scene.h"
#include "WC4BranchPak.h"

class WC4GameFlow : public IActivity {
public:
    WC4GameFlow();
    ~WC4GameFlow();

    void init();
    void runFrame(void);

private:
    enum class State {
        PLAY_ORIGIN_MOVIE,
        PLAY_OPENING_MOVIE,
        LOAD_SCENE,
        SCENE_ACTIVE,
        CONFIRM_QUIT,
        DONE
    };

    State state{State::PLAY_ORIGIN_MOVIE};
    int current_scene{0};
    bool savegame_exists{false};

    PakArchive shapePak;
    FrameBuffer* sceneFB{nullptr};
    WC4Scene* scene{nullptr};

    // Options
    bool opt_transition_videos{true};

    // Set after the player attends the mission briefing (scene 164).
    // Loadout (room 4) and wingman selection (room 7) are gated behind this.
    bool briefingDone{false};

    // SC_205.MVE is cached on first use; contains all room-transition and character-approach shots.
    std::vector<uint8_t> sc205Data;
    void ensureSC205();
    void playSC205Shot(int shotIndex);

    // BRANCH PAKs from cd1-4miss.tre: player dialogue (Blair voice lines + subtitles).
    // Index 0 = BRANCH1.PAK (cd1miss), 1 = BRANCH2.PAK, etc.
    WC4BranchPak branchPaks[4];
    void loadBranchPaks();
    void logRoomMenu() const;

    RSVGA& VGA = RSVGA::getInstance();
    AssetManager& Assets = AssetManager::getInstance();
    RSMixer& Mixer = RSMixer::getInstance();
    Keyboard* m_keyboard{nullptr};

    void playMovie(const char* name);
    void loadScene(int sceneId);
    void loadRoomPalette(int roomIndex);
    RSImageSet* getShape(int shapeId);
    std::unordered_map<int, RSImageSet*> shapeCache;

    GLuint sceneTexture{0};
    RLEShape* m_cursor{nullptr};
    bool m_cursor_owned{false};  // false when shape is owned by WC4Globals
    std::string m_hoverLabel;       // label of the zone under the cursor this frame
    uint32_t    m_hoveredGumpId{0}; // gump_id of zone under cursor (0 = none)
    uint32_t    m_hoveredZoneType{0};
    int         m_rightClickZoneIdx{-1}; // tracks which zone the right-click cycle is on
    void initSceneFramebuffer();
    void displaySceneFramebuffer();
    void drawCursor(int vx, int vy);
};

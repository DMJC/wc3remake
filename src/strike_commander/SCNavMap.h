//
//  SCNavMap.h
//  libRealSpace
//
//  Created by Rémi LEONARD on 31/08/2024.
//  Copyright (c) 2014 Fabien Sanglard. All rights reserved.
//

#pragma once
#include "precomp.h"

class SCNavMap : public IActivity {
    
public:
    SCNavMap();
    ~SCNavMap();
    
    void init();
    void SetName(char *name);
    void runFrame(void);
    RSMission *missionObj{nullptr};
    SCMission *mission{nullptr};
    uint8_t *current_nav_point;
    RSNavMap* navMap;
    
private:
    enum NavActionOfst {
        NAV_ESCAPE = 1,
        NAV_NEXT_WP = 2,
        NAV_PREV_WP = 3,
        // NAV_ESCAPE2 (formerly N, bound to closing the map) removed — N now
        // cycles waypoints instead (see checkKeyboard()'s own comment).
        // Plain ESCAPE (NAV_ESCAPE) and gamepad Back remain sufficient to close.
        NAV_ROTATE_LEFT = 5,
        NAV_ROTATE_RIGHT = 6,
        NAV_ROTATE_UP = 7,
        NAV_ROTATE_DOWN = 8,
    };
    std::unordered_map<int, Point2D> used_areas;
    int color{134};
    std::string *name;
    Keyboard *m_keyboard;

    VGAPalette objpal;

    bool show_area{false};
    bool show_obj{true};
    bool show_waypoint{true};

    void checkKeyboard(void);
    // Shared by keyboard (N/arrows) and gamepad (A/B) waypoint cycling — see
    // its own comment in SCNavMap.cpp.
    void advanceNavPoint(int delta);
    void showArea(AREA *area, float center, float map_width, int w, int h, int t, int l, int c);

    // --- 3D scene (skybox + grid + navpoint markers + player icon) ---
    // Orbit-camera state: rotate-around-a-fixed-point (world origin — see
    // render3DScene's own comment), driven by held-key input in
    // checkKeyboard(), pushed into Renderer.getCamera() fresh every frame in
    // render3DScene() (SCStrike is fully paused while this activity is on
    // top, so there's no per-frame camera owner to fight over, and nothing
    // needs restoring on close — SCStrike's own runFrame() sets its camera
    // from scratch, unconditionally, the instant flight resumes).
    float m_navAzimuth{0.0f};
    // Default view looks straight down at the grid (matches the real game
    // — see render3DScene's own comment on the azimuth-derived up vector
    // that keeps lookAt() well-defined even at this near-vertical angle,
    // avoiding the gimbal-lock case Camera::lookAt(Point3D*)'s fixed
    // (0,1,0) up would hit here).
    float m_navElevation{1.55f};
    float m_navDistance{0.0f}; // computed from map_width in init()/runFrame()
    uint32_t m_lastFrameTicks{0};

    // Cached GL texture for the player-ship billboard, rebuilt only when the
    // source RSImageSet pointer changes (mirrors SCRenderer::
    // buildStarSpriteTextures' own rebuild-only-on-change convention) rather
    // than re-uploading every frame.
    RSImageSet* m_playerIconTextureSource{nullptr};
    GLuint m_playerIconGLTex{0};
    int m_playerIconTexW{0};
    int m_playerIconTexH{0};

    // Renders the 3D scene (skybox, grid, navpoint reticles, player-ship
    // billboard) into a sub-viewport inset within the existing 2D border
    // art's map rectangle — called from runFrame() between VGA.vSync() and
    // drawWC3TextOverlay(). l/t/w/h are the same VGA-canvas-space box values
    // runFrame() already computes for the flat-map background image.
    void render3DScene(int l, int t, int w, int h, int canvasW, int canvasH);
    // Converts the VGA-canvas-space box (l,t,w,h) into real window pixels
    // via the same 4:3-letterboxed-viewport formula already used elsewhere
    // in this codebase (RSVGA::displayBuffer, SCCockpit's own overlay
    // passes) — see render3DScene's own comment on the Y-flip.
    void computeSubViewport(int l, int t, int w, int h, int canvasW, int canvasH,
                             int &vpX, int &vpY, int &vpW, int &vpH);
    // Single green GL_LINES batch, lineCount lines each direction, spanning
    // a square of side halfExtent*2 centered on the world origin.
    void drawNavGrid(float halfExtent, int lineCount);
    // Camera-facing circle+cross reticle at a world position, colored per
    // selection state (white=selected, red=otherwise) — see
    // WC3CockpitShapeId's own kTargetingReticle comment for why this is
    // hand-drawn geometry rather than that sprite (visually similar, not
    // confirmed to be the same asset).
    void drawNavMarkerReticle(const Vector3D &worldPos, const Vector3D &color,
                               float worldSize, const float camMv[16]);
    // Billboards nav.playerIcon at the player actor's real 3D position,
    // building/caching a GL texture from its indexed RLEShape pixels the
    // same way SCCockpit::RenderCockpitOverlayShape does for its own
    // overlay (index 255 forced transparent) — see render3DScene's call site.
    void drawPlayerBillboard(RSImageSet* iconSet, const Vector3D &worldPos, const float camMv[16]);
    // Redraws the WC3 nav-map border/frame art (MISN_NAV_RES::background —
    // the same full-canvas shape drawn early in runFrame(), underneath
    // everything) as a transparent-hole GL overlay (index 255 forced
    // alpha=0, same convention as SCCockpit::RenderCockpitOverlayShape) so
    // the border is guaranteed to composite on top of render3DScene's
    // content rather than being covered by it — called after render3DScene()
    // in runFrame().
    void drawBorderOverlay(RLEShape* borderShape, int canvasW, int canvasH);
    // SCMissionActorsPlayer::takeOff()/land() each synthesize a waypoint —
    // objective == "take off" / "landing" respectively — representing the
    // mission's own launch/landing points, never real player-facing
    // navpoints. Excluded from both the visible marker list and N/arrow-key
    // cycling. (Name kept as "isTakeoffWaypoint" despite covering both —
    // it's the hidden-synthetic-waypoint check, not literally takeoff-only.)
    bool isTakeoffWaypoint(SCMissionWaypoint *wp) const;

protected:
    // Loaded once in init() from DATA\PALETTE\PALETTE.IFF — protected
    // (not private) so WC3NavMap::drawWC3TextOverlay can nearest-match
    // WC3Font's real RGBA pixel colors against it (see that function's own
    // comment for why: everything on this screen, including WC3-native
    // text, draws through the same palette-indexed FrameBuffer rather than
    // a second GL pass).
    VGAPalette palette;
    // Called once per frame at the very end of runFrame() (after the
    // normal VGA.vSync() present), unconditionally — empty by default (SC1
    // cockpits have a real RSFont and draw their own title/mission-name/
    // objective/notes text directly via FrameBuffer::printText_SM inside
    // runFrame() itself, so there's nothing extra to add here). WC3NavMap
    // (wing_commander_3/) overrides this to draw the same kind of info via
    // WC3Font instead, since WC3 ships no RSFont-compatible font asset at
    // all (see showArea's own comment). Layered as a separate, additive GL
    // pass on top of whatever VGA.vSync() already presented this frame —
    // confirmed safe since RSVGA::displayBuffer/vSync never call
    // SDL_GL_SwapWindow themselves (that happens later in GameEngine::
    // run()'s own loop) — rather than needing WC3Font/WC3Globals
    // (wing_commander_3/) included directly in this file, which would
    // create a strike_commander -> wing_commander_3 dependency this
    // codebase avoids everywhere else (see SCStrike::
    // checkGameSpecificKeyboard's own comment on the same constraint, and
    // SCStrike::createNavMap() for the matching construction-side hook
    // that lets WC3Strike hand back a WC3NavMap instead of a plain
    // SCNavMap in the first place).
    virtual void drawWC3TextOverlay(float center, float map_width, int w, int h, int t, int l) {}
};
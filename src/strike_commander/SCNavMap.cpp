//
//  SCNavMap.cpp
//  libRealSpace
//
//  Created by Rémi LEONARD on 31/08/2024.
//  Copyright (c) 2014 Fabien Sanglard. All rights reserved.
//
#include "precomp.h"
#include "../commons/GraphicsSettings.h"
#include "../engine/GLBatch.h"

namespace {
    struct LabelBox {
        int x,y,w,h;
    };
    static std::vector<LabelBox> g_labelBoxes;

    inline bool intersects(const LabelBox &a, const LabelBox &b) {
        return !(a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y);
    }

    Point2D placeLabel(int anchorX, int anchorY, int w, int h) {
        // Positions candidates (dx, dy appliqués au point d’ancrage)
        static const std::vector<std::pair<int,int>> candidates = {
            { 0, 0},
            { 0,-h },
            { 0, h },
            { 0, h},
            { w, 0 },
            { w,-h },
            { w, h },
            { -w, 0},
            { -w,-h },
            { -w, h }
        };
        for (auto &c : candidates) {
            LabelBox box { anchorX + c.first, anchorY + c.second, w, h };
            bool clash = false;
            for (auto &b : g_labelBoxes) {
                if (intersects(box, b)) { clash = true; break; }
            }
            if (!clash) {
                g_labelBoxes.push_back(box);
                return Point2D{ box.x, box.y };
            }
        }
        // Fallback : empiler vers le bas
        int yOff = 6;
        while (yOff < 60) {
            LabelBox box { anchorX - w/2, anchorY + yOff, w, h };
            bool clash = false;
            for (auto &b : g_labelBoxes) {
                if (intersects(box, b)) { clash = true; break; }
            }
            if (!clash) {
                g_labelBoxes.push_back(box);
                return Point2D{ box.x, box.y };
            }
            yOff += h + 2;
        }
        // Dernier recours : ne pas enregistrer (risque chevauchement)
        return Point2D{ anchorX - w/2, anchorY + 4 };
    }

    inline void ResetLabelBoxes() {
        g_labelBoxes.clear();
    }
}

SCNavMap::SCNavMap(){
    this->navMap = nullptr;
    if (Game == nullptr) {
        Game = &GameEngine::instance();
    }
}
SCNavMap::~SCNavMap(){

}
/**
 * Checks for keyboard events and updates the game state accordingly.
 *
 * This function peeks at the SDL event queue to check for keyboard events.
 * If a key is pressed, it updates the game state based on the key pressed.
 * The supported keys are escape, space, and return.
 *
 * @return None
 */
// Advances *current_nav_point by delta (+1/-1), wrapping around the ends of
// mission->waypoints, and skipping any isTakeoffWaypoint() entry — that
// hidden launch-point waypoint is never a real, player-facing selection.
// Shared by keyboard (N/arrows) and gamepad (A/B) cycling.
void SCNavMap::advanceNavPoint(int delta) {
    if (this->mission == nullptr || this->mission->waypoints.empty()) {
        if (this->current_nav_point) *this->current_nav_point = 0;
        return;
    }
    int n = (int)this->mission->waypoints.size();
    int idx = (int)*this->current_nav_point;
    // Bounded by n so an all-takeoff waypoint list (shouldn't happen — a
    // mission always has real objectives — but degenerate data shouldn't
    // hang) can't loop forever.
    for (int guard = 0; guard < n; guard++) {
        idx += delta;
        if (idx < 0) idx = n - 1;
        if (idx >= n) idx = 0;
        if (!isTakeoffWaypoint(this->mission->waypoints[idx])) break;
    }
    *this->current_nav_point = (uint8_t)idx;
}

void SCNavMap::checkKeyboard(void) {
    if (this->m_keyboard == nullptr) {
        return;
    }
    if (this->m_keyboard->isActionJustPressed(CreateAction(InputAction::NAVMAP_START, NavActionOfst::NAV_ESCAPE))) {
        Game->stopTopActivity();
    }
    if (this->m_keyboard->isActionJustPressed(InputAction::CONTROLLER_BUTTON_BACK)) {
        Game->stopTopActivity();
    }
    // N (bound to NAV_NEXT_WP, see init()) and the arrow keys both cycle
    // waypoints now — N used to close the map (NAV_ESCAPE2), which
    // contradicted the real game's own N-cycles-waypoints convention.
    if (this->m_keyboard->isActionJustPressed(CreateAction(InputAction::NAVMAP_START, NavActionOfst::NAV_NEXT_WP)) ||
        this->m_keyboard->isActionJustPressed(InputAction::CONTROLLER_BUTTON_A)) {
        advanceNavPoint(+1);
    }
    if (this->m_keyboard->isActionJustPressed(CreateAction(InputAction::NAVMAP_START, NavActionOfst::NAV_PREV_WP)) ||
        this->m_keyboard->isActionJustPressed(InputAction::CONTROLLER_BUTTON_B)) {
        advanceNavPoint(-1);
    }

    // Orbit-camera rotation — held (not edge-triggered), continuous,
    // delta-time-scaled so rotation speed doesn't depend on frame rate.
    // See m_navAzimuth/m_navElevation's own comment in SCNavMap.h.
    uint32_t nowTicks = SDL_GetTicks();
    float dt = (m_lastFrameTicks != 0) ? (float)(nowTicks - m_lastFrameTicks) / 1000.0f : 0.0f;
    m_lastFrameTicks = nowTicks;
    constexpr float kRotSpeed = 1.5f; // radians/sec
    if (this->m_keyboard->isActionPressed(CreateAction(InputAction::NAVMAP_START, NavActionOfst::NAV_ROTATE_LEFT)))
        m_navAzimuth -= kRotSpeed * dt;
    if (this->m_keyboard->isActionPressed(CreateAction(InputAction::NAVMAP_START, NavActionOfst::NAV_ROTATE_RIGHT)))
        m_navAzimuth += kRotSpeed * dt;
    if (this->m_keyboard->isActionPressed(CreateAction(InputAction::NAVMAP_START, NavActionOfst::NAV_ROTATE_UP)))
        m_navElevation += kRotSpeed * dt;
    if (this->m_keyboard->isActionPressed(CreateAction(InputAction::NAVMAP_START, NavActionOfst::NAV_ROTATE_DOWN)))
        m_navElevation -= kRotSpeed * dt;
    // render3DScene() now builds its lookAt() up vector from azimuth alone
    // (never parallel to the view direction, even straight down), so this
    // no longer needs to dodge exact vertical — clamped just a hair short
    // of +/-PI/2 purely to avoid float noise flipping the sign of a
    // near-zero horizontal camera offset.
    constexpr float kElevLimit = 1.55f;
    if (m_navElevation > kElevLimit) m_navElevation = kElevLimit;
    if (m_navElevation < -kElevLimit) m_navElevation = -kElevLimit;
}
/**
 * Initializes the SCNavMap object.
 *
 * This function reads in the PALETTE.IFF file and sets the object's color palette.
 * It also reads in the NAMAP.IFF file and sets the object's navMap property.
 *
 * @return None
 */
void SCNavMap::init(){
    RSPalette palette;
    TreEntry *entries = (TreEntry *)Assets.GetEntryByName("..\\..\\DATA\\PALETTE\\PALETTE.IFF");
    if (entries != nullptr) {
        palette.initFromFileRam(entries->data, entries->size);
    } else {
        FileData *f = Assets.GetFileData("PALETTE.IFF");
        if (f != nullptr) {
            palette.initFromFileData(f);    
        }
    }
    this->palette = *palette.GetColorPalette();
    for (uint8_t i=0; i<255; i++) {
        Texel *c = new Texel({i,0,0,255});
        this->objpal.SetColor(i, c);
    }
    
    // Assets.navmap_filename (DATA\COCKPITS\NAVMAP.IFF) is a Strike
    // Commander (SC1) asset path — confirmed absent from every WC3 TRE
    // catalog (GAMEFLOW/MISSIONS/OBJECTS/INSTALL/MOVIES), so GetEntryByName
    // always returns null for WC3 missions. This used to be dereferenced
    // unconditionally here, crashing the instant a player pressed N in
    // flight. WC3 has no equivalent standalone nav-map image asset (see
    // showArea/showWaypoint below for the WC3-native replacement instead —
    // this now draws waypoints/areas procedurally over a plain background
    // rather than a real map image when nav_map is unavailable).
    TreEntry *nav_map = Assets.GetEntryByName(Assets.navmap_filename);
    if (nav_map != nullptr) {
        this->navMap = new RSNavMap();
        this->navMap->InitFromRam(nav_map->data, nav_map->size);
    } else {
        this->navMap = nullptr;
    }

    if (nav_map != nullptr && Assets.navmap_add_filename != "") {
        TreEntry *nav_map_add = Assets.GetEntryByName(Assets.navmap_add_filename);
        if (nav_map_add != nullptr) {
            RSNavMap *nav_map_add_obj = new RSNavMap();
            nav_map_add_obj->InitFromRam(nav_map_add->data, nav_map_add->size);
            for (auto &map: nav_map_add_obj->maps) {
                this->navMap->maps[map.first] = map.second;
            }
        }
    }
    m_keyboard = Game->getKeyboard();
    m_keyboard->registerAction(CreateAction(InputAction::NAVMAP_START, NavActionOfst::NAV_ESCAPE));
    m_keyboard->registerAction(CreateAction(InputAction::NAVMAP_START, NavActionOfst::NAV_NEXT_WP));
    m_keyboard->registerAction(CreateAction(InputAction::NAVMAP_START, NavActionOfst::NAV_PREV_WP));
    m_keyboard->registerAction(CreateAction(InputAction::NAVMAP_START, NavActionOfst::NAV_ROTATE_LEFT));
    m_keyboard->registerAction(CreateAction(InputAction::NAVMAP_START, NavActionOfst::NAV_ROTATE_RIGHT));
    m_keyboard->registerAction(CreateAction(InputAction::NAVMAP_START, NavActionOfst::NAV_ROTATE_UP));
    m_keyboard->registerAction(CreateAction(InputAction::NAVMAP_START, NavActionOfst::NAV_ROTATE_DOWN));


    m_keyboard->bindKeyToAction(CreateAction(InputAction::NAVMAP_START, NavActionOfst::NAV_ESCAPE), SDL_SCANCODE_ESCAPE);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::NAVMAP_START, NavActionOfst::NAV_PREV_WP), SDL_SCANCODE_LEFT);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::NAVMAP_START, NavActionOfst::NAV_NEXT_WP), SDL_SCANCODE_RIGHT);
    // N cycles waypoints now (matches the real game — see checkKeyboard's
    // own comment) rather than closing the map; bound alongside the arrow
    // keys on the same action (bindKeyToAction is additive, confirmed in
    // InputActionSystem::bindInput).
    m_keyboard->bindKeyToAction(CreateAction(InputAction::NAVMAP_START, NavActionOfst::NAV_NEXT_WP), SDL_SCANCODE_N);
    // Orbit-camera rotation — WASD, free on this screen (arrows/N/ESCAPE are
    // the only other bindings here; this isn't the flight-control screen).
    m_keyboard->bindKeyToAction(CreateAction(InputAction::NAVMAP_START, NavActionOfst::NAV_ROTATE_LEFT), SDL_SCANCODE_A);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::NAVMAP_START, NavActionOfst::NAV_ROTATE_RIGHT), SDL_SCANCODE_D);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::NAVMAP_START, NavActionOfst::NAV_ROTATE_UP), SDL_SCANCODE_W);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::NAVMAP_START, NavActionOfst::NAV_ROTATE_DOWN), SDL_SCANCODE_S);

}
/**
 * Sets the name of the current mission.
 *
 * This function takes a C-style string as its argument and sets the object's
 * name to that string.
 *
 * @param name the name of the current mission
 */
void SCNavMap::SetName(char *name) {
    this->name = new std::string(name);
    std::transform(this->name->begin(), this->name->end(), this->name->begin(), ::toupper);
}
/**
 * SCNavMap::RunFrame
 *
 * This function runs the frame of the navigation map.
 *
 * It clears the screen, sets the palette, draws the background and the shape
 * corresponding to the current mission, and draws the mouse.
 *
 * @return None
 */
void SCNavMap::runFrame(void) {
    this->checkKeyboard();
    bool upscaled = VGA.upscale;
    VGA.upscale = false;
    VGA.activate();
    VGA.getFrameBuffer()->clear();
    ResetLabelBoxes();
    VGA.setPalette(&this->palette);
    
    //Draw static
    used_areas.clear();
    float center = BLOCK_WIDTH * 9.0f;
    float map_width = BLOCK_WIDTH * 18.0f;
    RLEShape *map_shape = nullptr;
    if (this->navMap != nullptr && this->name != nullptr) {
        if (this->navMap->maps.count(*this->name)>0) {
            map_shape = this->navMap->maps[*this->name];
        } else if (this->navMap->maps.count(this->mission->mission->mission_data.world_filename)>0) {
            map_shape = this->navMap->maps[this->mission->mission->mission_data.world_filename];
        }
    }
    if (map_shape != nullptr) {
        VGA.getFrameBuffer()->drawShape(map_shape);
        RLEShape *shape = this->navMap->background;
        if (shape != nullptr) {
            VGA.getFrameBuffer()->drawShape(shape);
        }
        Point2D pos = map_shape->position;
        int w = 238;
        int h = 155;
        int t = 18;
        int l = 6;
        
        std::string leader = "solo";
        if (this->mission->friendlies.size() > 0) {
            leader = this->mission->friendlies[0]->profile->radi.info.callsign;
        }
        std::string mission_name = this->mission->mission->mission_data.world_filename;
        std::transform(leader.begin(), leader.end(), leader.begin(), ::tolower);
        std::transform(mission_name.begin(), mission_name.end(), mission_name.begin(), ::tolower);
        int msg_newx = 257;
        int msg_newy = 10;
        Point2D mission_name_pos{248, 30};
        Point2D leader_pos{248, 50};

        VGA.getFrameBuffer()->printText_SM(
            this->navMap->font,
            &mission_name_pos,
            const_cast<char*>("mission"),
            0,
            0,
            7,1,this->navMap->font->GetShapeForChar('A')->GetWidth(), false, false);
        
        mission_name_pos.x = 260;
        mission_name_pos.y += this->navMap->font->GetShapeForChar('A')->GetHeight();

        VGA.getFrameBuffer()->printText_SM(
            this->navMap->font,
            &mission_name_pos,
            (char*) mission_name.c_str(),
            0,
            0,
            (int32_t)mission_name.size(),1,this->navMap->font->GetShapeForChar('A')->GetWidth(), false, false);
        leader_pos.y = mission_name_pos.y + this->navMap->font->GetShapeForChar('A')->GetHeight();
        VGA.getFrameBuffer()->printText_SM(
            this->navMap->font,
            &leader_pos,
            const_cast<char*>("leader"),
            0,
            0,
            6,1,this->navMap->font->GetShapeForChar('A')->GetWidth(), false, false);
        leader_pos.y += this->navMap->font->GetShapeForChar('A')->GetHeight();
        leader_pos.x = 260;
        VGA.getFrameBuffer()->printText_SM(
            this->navMap->font,
            &leader_pos,
            (char*) leader.c_str(),
            0,
            0,
            (int32_t)leader.size(),1,this->navMap->font->GetShapeForChar('A')->GetWidth(), false, false);
        if (this->mission->friendlies.size() > 1) {
            if (this->mission->friendlies[1]->profile != nullptr) {
                leader = this->mission->friendlies[1]->profile->radi.info.callsign;
            } else {
                leader = "wingman";
            }
            std::transform(leader.begin(), leader.end(), leader.begin(), ::tolower);
            leader_pos.y += this->navMap->font->GetShapeForChar('A')->GetHeight();
            leader_pos.x = 252;
            VGA.getFrameBuffer()->printText_SM(
                this->navMap->font,
                &leader_pos,
                const_cast<char*>("wingman"),
                0,
                0,
                7,1,this->navMap->font->GetShapeForChar('A')->GetWidth(), false, false);
            leader_pos.y += this->navMap->font->GetShapeForChar('A')->GetHeight();
            leader_pos.x = 260;
            VGA.getFrameBuffer()->printText_SM(
                this->navMap->font,
                &leader_pos,
                (char*) leader.c_str(),
                0,
                0,
                (int32_t)leader.size(),1,this->navMap->font->GetShapeForChar('A')->GetWidth(), false, false);
        }
        
        if (show_waypoint) {
            int cpt = 0;
            
            for (auto wp: this->mission->waypoints) {
                int newx = (int) (((wp->spot->position.x+center)/map_width)*w)+l;
                int newy = (int) (((wp->spot->position.z+center)/map_width)*h)+t;
                int c = 134;
                if (cpt == *this->current_nav_point) {
                    int msg_newx = 252;
                    int msg_newy = 80;
                    Point2D *msg_p1 = new Point2D({msg_newx, msg_newy});
                    
                    VGA.getFrameBuffer()->printText_SM(
                        this->navMap->font,
                        msg_p1,
                        const_cast<char*>("objective"),
                        0,
                        0,
                        9,
                        1,
                        this->navMap->font->GetShapeForChar('A')->GetWidth(),
                        false, false
                    );
                    
                    msg_p1->y += this->navMap->font->GetShapeForChar('A')->GetHeight();
                    msg_p1->x = msg_newx+5;
                    if (wp->objective != nullptr) {
                        std::transform(wp->objective->begin(), wp->objective->end(), wp->objective->begin(), ::tolower);
                        VGA.getFrameBuffer()->printText_SM(
                            this->navMap->font,
                            msg_p1,
                            (char*) wp->objective->c_str(),
                            0,
                            0,
                            (int32_t)wp->objective->size(),
                            1,
                            this->navMap->font->GetShapeForChar('A')->GetWidth(),
                            false, false
                        );
                    }

                    msg_newx = 255;
                    msg_newy = 135;
                    msg_p1 = new Point2D({msg_newx, msg_newy});
                    
                    VGA.getFrameBuffer()->printText_SM(
                        this->navMap->font,
                        msg_p1,
                        const_cast<char*>("notes"),
                        0,
                        0,
                        5,
                        1,
                        this->navMap->font->GetShapeForChar('A')->GetWidth(),
                        false, false
                    );
                    msg_p1->y += this->navMap->font->GetShapeForChar('A')->GetHeight();
                    msg_p1->x = msg_newx+3;
                    if (wp->message != nullptr) {
                        std::transform(wp->message->begin(), wp->message->end(), wp->message->begin(), ::tolower);
                        VGA.getFrameBuffer()->printText_SM(
                            this->navMap->font,
                            msg_p1,
                            (char*) wp->message->c_str(),
                            0,
                            0,
                            (int32_t)wp->message->size(),
                            1,
                            this->navMap->font->GetShapeForChar('A')->GetWidth(),
                            true, false
                        );
                    }
                    c = 255;
                    if (wp->spot->area_id >= 0 && wp->spot->area_id < this->missionObj->mission_data.areas.size()) {
                        newx = (int) (((this->missionObj->mission_data.areas[wp->spot->area_id]->position.x+center)/map_width)*w)+l;
                        newy = (int) (((this->missionObj->mission_data.areas[wp->spot->area_id]->position.z+center)/map_width)*h)+t;
                        if (newx>0 && newx<VGA.GetCanvasWidth() && newy>0 && newy<VGA.GetCanvasHeight()) {
                            VGA.getFrameBuffer()->plot_pixel(newx, newy, 128);
                            VGA.getFrameBuffer()->circle_slow(newx, newy, 3, 1);
                        }
                        this->showArea(this->missionObj->mission_data.areas[wp->spot->area_id], center, map_width, w, h, t, l, c);
                    }   
                    
                } else {
                    if (wp->spot->area_id != 255 && wp->spot->area_id >= 0 && wp->spot->area_id < this->missionObj->mission_data.areas.size()) {
                        newx = (int) (((this->missionObj->mission_data.areas[wp->spot->area_id]->position.x+center)/map_width)*w)+l;
                        newy = (int) (((this->missionObj->mission_data.areas[wp->spot->area_id]->position.z+center)/map_width)*h)+t;
                        if (used_areas.find(wp->spot->area_id) == used_areas.end()) {
                            
                            if (newx>0 && newx<VGA.GetCanvasWidth() && newy>0 && newy<VGA.GetCanvasHeight()) {
                                VGA.getFrameBuffer()->plot_pixel(newx, newy, 128);
                                VGA.getFrameBuffer()->circle_slow(newx, newy, 3, 1);
                            }
                            this->showArea(this->missionObj->mission_data.areas[wp->spot->area_id], center, map_width, w, h, t, l, c);
                        }                    
                    }
                    
                }
                cpt++;
            }
        }
        if (show_obj) {
            for (auto friends : this->mission->friendlies) {
                if (friends->is_active == 0 && friends->actor_name != "PLAYER") continue;
                int newx = (int) (((friends->object->position.x+center)/map_width)*w)+l;
                int newy = (int) (((friends->object->position.z+center)/map_width)*h)+t;
                if (newx>0 && newx<VGA.GetCanvasWidth() && newy>0 && newy<VGA.GetCanvasHeight()) {
                    

                    std::string name = (friends->profile != nullptr) ? friends->profile->radi.info.callsign : "FRIEND";
                    std::transform(name.begin(), name.end(), name.begin(), ::toupper);

                    int glyphW = this->navMap->font->GetShapeForChar('A')->GetWidth();
                    int glyphH = this->navMap->font->GetShapeForChar('A')->GetHeight();
                    int labelW = (int)name.size() * (glyphW + 1);
                    int labelH = glyphH;

                    Point2D obj_pos = placeLabel(newx-labelW, newy+glyphH, labelW, labelH);
                    VGA.getFrameBuffer()->printText_SM(
                        this->navMap->font,
                        &obj_pos,
                        (char*) name.c_str(),
                        34,
                        0,
                        (int32_t)name.size(),1,glyphW,true,false
                    );
                    VGA.getFrameBuffer()->plot_pixel(newx, newy, 10);
                    VGA.getFrameBuffer()->circle_slow(newx, newy, 2, 1);
                }
            }
        }
    } else if (this->missionObj != nullptr && this->mission != nullptr) {
        // WC3 path: MISN>NAV>VGA (see RSMission.h's MISN_NAV_RES comment)
        // carries a real per-mission background image and player-position
        // icon — both plain "1.11" WC3 shapes (RSWC3Shape.h), the same
        // format/decoder already used for cockpit instrument art, so they
        // draw through the exact same FrameBuffer::drawShape() path the
        // SC1 background above uses. No RSFont exists for WC3 though (see
        // showArea's own comment), so text labels still aren't drawn here
        // — see drawWC3TextOverlay() for those instead, layered on as a
        // separate WC3Font-based pass after this whole function returns.
        // Layout box: real VGA values (238,155,18,6) come straight from the
        // SC1 code above. SVGA's (476,372,43,12) are those same values
        // proportionally scaled to the 640x480 canvas (not ground truth —
        // there's no equivalent hardcoded reference for SVGA the way SC1
        // provided one for VGA — but SVGA isn't a clean 2x of VGA either,
        // see RSCockpit.h's own ARTP_SVGA comment, so a straight 2x would
        // be wrong too).
        int w = g_ifVGA ? 238 : 476, h = g_ifVGA ? 155 : 372, t = g_ifVGA ? 18 : 43, l = g_ifVGA ? 6 : 12;
        MISN_NAV_RES &nav = g_ifVGA ? this->missionObj->mission_data.navVGA : this->missionObj->mission_data.navSVGA;
        if (nav.background != nullptr && nav.background->GetNumImages() > 0) {
            VGA.getFrameBuffer()->drawShape(nav.background->GetShape(0));
        } else {
            // Fallback for the (so far unseen, but not proven impossible)
            // case of a mission with no NAV chunk at all — better than a
            // blank screen.
            VGA.getFrameBuffer()->rect_slow(l, t, l + w, t + h, 8);
            for (int gx = l; gx <= l + w; gx += 20) {
                VGA.getFrameBuffer()->line(gx, t, gx, t + h, 8);
            }
            for (int gy = t; gy <= t + h; gy += 20) {
                VGA.getFrameBuffer()->line(l, gy, l + w, gy, 8);
            }
        }
        if (show_waypoint) {
            int cpt = 0;
            for (auto wp: this->mission->waypoints) {
                int c = (cpt == *this->current_nav_point) ? 255 : 134;
                if (wp->spot->area_id != 255 && wp->spot->area_id >= 0 &&
                    wp->spot->area_id < this->missionObj->mission_data.areas.size() &&
                    (cpt == *this->current_nav_point || used_areas.find(wp->spot->area_id) == used_areas.end())) {
                    int newx = (int) (((this->missionObj->mission_data.areas[wp->spot->area_id]->position.x+center)/map_width)*w)+l;
                    int newy = (int) (((this->missionObj->mission_data.areas[wp->spot->area_id]->position.z+center)/map_width)*h)+t;
                    if (newx>0 && newx<VGA.GetCanvasWidth() && newy>0 && newy<VGA.GetCanvasHeight()) {
                        VGA.getFrameBuffer()->plot_pixel(newx, newy, 128);
                        VGA.getFrameBuffer()->circle_slow(newx, newy, 3, 1);
                    }
                    this->showArea(this->missionObj->mission_data.areas[wp->spot->area_id], center, map_width, w, h, t, l, c);
                }
                cpt++;
            }
        }
        if (show_obj) {
            for (auto friends : this->mission->friendlies) {
                if (friends->is_active == 0 && friends->actor_name != "PLAYER") continue;
                int newx = (int) (((friends->object->position.x+center)/map_width)*w)+l;
                int newy = (int) (((friends->object->position.z+center)/map_width)*h)+t;
                if (newx>0 && newx<VGA.GetCanvasWidth() && newy>0 && newy<VGA.GetCanvasHeight()) {
                    if (friends->actor_name == "PLAYER" && nav.playerIcon != nullptr && nav.playerIcon->GetNumImages() > 0) {
                        RLEShape *icon = nav.playerIcon->GetShape(0);
                        Point2D iconPos = {newx - icon->GetWidth() / 2, newy - icon->GetHeight() / 2};
                        icon->SetPosition(&iconPos);
                        VGA.getFrameBuffer()->drawShape(icon);
                    } else {
                        VGA.getFrameBuffer()->plot_pixel(newx, newy, 10);
                        VGA.getFrameBuffer()->circle_slow(newx, newy, 2, 1);
                    }
                }
            }
        }
    }

    Mouse.draw();
    VGA.vSync();
    // 3D scene (skybox/grid/navpoint reticles/player billboard) — a
    // separate, additive GL pass composited on top of whatever VGA.vSync()
    // just presented, inset into the same VGA-canvas-space box (l,t,w,h)
    // the flat 2D background art above occupies. Same VGA/SVGA layout
    // constants as the WC3 branch above (see that branch's own comment on
    // the SVGA values being a proportional, not ground-truth, scale) —
    // recomputed here since those locals are scoped inside the if/else-if
    // above, the same reason drawWC3TextOverlay's call below recomputes them.
    this->render3DScene(g_ifVGA ? 6 : 12, g_ifVGA ? 18 : 43, g_ifVGA ? 238 : 476, g_ifVGA ? 155 : 372,
                         VGA.GetCanvasWidth(), VGA.GetCanvasHeight());
    // Re-assert the WC3 border/frame art on top of the 3D scene just drawn
    // above (see drawBorderOverlay's own comment for why it would otherwise
    // get covered) — recomputed nav the same way the box constants above
    // are, since it's scoped inside the if/else-if higher up.
    if (this->missionObj != nullptr) {
        MISN_NAV_RES &borderNav = g_ifVGA ? this->missionObj->mission_data.navVGA : this->missionObj->mission_data.navSVGA;
        if (borderNav.background != nullptr && borderNav.background->GetNumImages() > 0) {
            this->drawBorderOverlay(borderNav.background->GetShape(0), VGA.GetCanvasWidth(), VGA.GetCanvasHeight());
        }
    }
    // Always called, empty by default for SC1 (see the hook's own comment
    // in SCNavMap.h) — WC3NavMap's override draws its WC3Font-based title/
    // mission-name/objective/notes text as a separate additive GL pass on
    // top of whatever VGA.vSync() just presented.
    // Same VGA/SVGA layout constants as the WC3 branch above — see that
    // branch's own comment on the SVGA values being a proportional (not
    // ground-truth) scale.
    this->drawWC3TextOverlay(center, map_width, g_ifVGA ? 238 : 476, g_ifVGA ? 155 : 372, g_ifVGA ? 18 : 43, g_ifVGA ? 6 : 12);
    VGA.upscale = upscaled;
}

void SCNavMap::showArea(AREA *area, float center, float map_width, int w, int h, int t, int l, int c) {
    int newx = (int) (((area->position.x+center)/map_width)*w)+l;
    int newy = (int) (((area->position.z+center)/map_width)*h)+t;
    int neww = (int) ((area->AreaWidth / map_width) * w);
    int newh = neww;
    bool isCircle = true;
    if (area->AreaWidth != area->AreaHeight && area->AreaType != 'S') {
        newh = (int) ((area->AreaHeight / map_width) * h);
        isCircle = false;
    }
    if (newx>0 && newx<VGA.GetCanvasWidth() && newy>0 && newy<VGA.GetCanvasHeight()) {
        if (isCircle) {
            VGA.getFrameBuffer()->circle_slow(newx, newy, neww, c);
        } else if (area->AreaType != 'C') {
            VGA.getFrameBuffer()->plot_pixel(newx, newy, 1);
            VGA.getFrameBuffer()->line(newx-neww, newy-newh, newx+neww, newy-newh, c);
            VGA.getFrameBuffer()->line(newx-neww, newy+newh, newx+neww, newy+newh, c);
            VGA.getFrameBuffer()->line(newx-neww, newy-newh, newx-neww, newy+newh, c);
            VGA.getFrameBuffer()->line(newx+neww, newy-newh, newx+neww, newy+newh, c);
        } else if (area->AreaType == 'C') {
            VGA.getFrameBuffer()->circle_slow(newx, newy, newh+1, c);
        }
        // Area-name text label needs this->navMap->font (RSFont) — a
        // resource this codebase only ever loads from Strike Commander
        // (SC1) assets (DATA\FONTS\*.SHP). WC3's data (checked across every
        // TRE catalog) has no equivalent RSFont-compatible font file at
        // all — its own fonts are a different class (WC3Font, embedded in
        // globals.iff) that this FrameBuffer::printText pipeline can't use.
        // Bridging the two font systems is real, separate work; until
        // then, WC3 missions get the area's shape (drawn above,
        // unconditionally) without its name label rather than crashing on
        // a null navMap->font.
        if (this->navMap == nullptr) {
            return;
        }
        int txtw = (int) strlen(area->AreaName) * (this->navMap->font->GetShapeForChar('A')->GetWidth()+1);
        int txtl = (int) strlen(area->AreaName);
        Point2D *p1 = new Point2D({newx-(txtw/2)<0?newx:newx-(txtw/2), newy});
        int glyphW = this->navMap->font->GetShapeForChar('A')->GetWidth();
        int glyphH = this->navMap->font->GetShapeForChar('A')->GetHeight();
        int labelW = (int)strlen(area->AreaName) * (glyphW + 1);
        int labelH = glyphH;

        Point2D area_pos;
        if (used_areas.find(area->id-1) != used_areas.end()) {
            area_pos = used_areas[area->id-1];
        } else {
            area_pos = placeLabel(p1->x, p1->y, labelW, labelH);
            used_areas[area->id-1]=area_pos;
        }
        VGA.getFrameBuffer()->printText(
            this->navMap->font,
            &area_pos,
            area->AreaName,
            c,
            0,
            txtl,1,this->navMap->font->GetShapeForChar('A')->GetWidth()
        );
    }
}

// SCMissionActorsPlayer::takeOff()/land() (SCMissionActors.cpp) each push a
// synthetic waypoint onto mission->waypoints representing the mission's own
// launch/landing point, not a real player-facing navpoint — objective is
// exactly "take off" or "landing" (byte-confirmed against the source, not
// guessed). Excluded from both the 3D marker list and N/arrow-key cycling.
bool SCNavMap::isTakeoffWaypoint(SCMissionWaypoint *wp) const {
    return SCMissionWaypoint::IsTakeoffOrLanding(wp);
}

// Converts the VGA-canvas-space box (l,t,w,h) — the same box the flat 2D
// nav-map background art occupies — into real window pixels, via the exact
// letterbox formula RSVGA::displayBuffer/SCCockpit's own overlay passes
// already use (letterboxW = winH*4/3, centered horizontally; the canvas
// fills the full window height, no vertical letterboxing). The canvas'
// Y-down convention is flipped to GL's Y-up-from-bottom viewport convention
// by measuring from the box's bottom edge (t+h) rather than its top.
void SCNavMap::computeSubViewport(int l, int t, int w, int h, int canvasW, int canvasH,
                                   int &vpX, int &vpY, int &vpW, int &vpH) {
    int winW = VGA.GetWindowWidth();
    int winH = VGA.GetWindowHeight();
    int letterboxW = (int)((float)winH * (4.0f / 3.0f));
    int letterboxX = (winW - letterboxW) / 2;

    float scaleX = (canvasW > 0) ? (float)letterboxW / (float)canvasW : 1.0f;
    float scaleY = (canvasH > 0) ? (float)winH / (float)canvasH : 1.0f;

    vpX = letterboxX + (int)((float)l * scaleX);
    vpW = (int)((float)w * scaleX);
    vpH = (int)((float)h * scaleY);
    vpY = winH - (int)((float)(t + h) * scaleY);
}

// Single GL_LINES batch: lineCount lines each direction, spanning a square
// of side halfExtent*2 centered on the world origin — the same origin/scale
// the flat 2D map already normalizes around (center = BLOCK_WIDTH*9.0f,
// map_width = BLOCK_WIDTH*18.0f in runFrame()), so the 3D grid lines up
// with where the 2D grid used to be.
//
// Colors sampled directly from the real game, not guessed: the two lines
// crossing the origin are NOT plain grid-green — looking top-down, the
// vertical one (constant X, varying Z) is a pale blue and the horizontal
// one (constant Z, varying X) is a brighter green than the rest of the
// grid, which is a darker green. lineCount is odd (17) specifically so the
// center index (lineCount/2) lands exactly on t=0.
void SCNavMap::drawNavGrid(float halfExtent, int lineCount) {
    glDisable(GL_TEXTURE_2D);
    const float gridR = 60.0f / 255.0f, gridG = 113.0f / 255.0f, gridB = 40.0f / 255.0f;
    const float centerVertR = 113.0f / 255.0f, centerVertG = 166.0f / 255.0f, centerVertB = 207.0f / 255.0f;
    const float centerHorizR = 73.0f / 255.0f, centerHorizG = 215.0f / 255.0f, centerHorizB = 20.0f / 255.0f;
    int centerIdx = lineCount / 2;
    gb.begin(GL_LINES);
    for (int i = 0; i < lineCount; i++) {
        float t = (lineCount <= 1) ? 0.0f
            : (-halfExtent + (2.0f * halfExtent) * ((float)i / (float)(lineCount - 1)));
        bool isCenter = (i == centerIdx);
        gb.color3f(isCenter ? centerVertR : gridR, isCenter ? centerVertG : gridG, isCenter ? centerVertB : gridB);
        gb.vertex3f(t, 0.0f, -halfExtent);
        gb.vertex3f(t, 0.0f,  halfExtent);
        gb.color3f(isCenter ? centerHorizR : gridR, isCenter ? centerHorizG : gridG, isCenter ? centerHorizB : gridB);
        gb.vertex3f(-halfExtent, 0.0f, t);
        gb.vertex3f( halfExtent, 0.0f, t);
    }
    gb.end();
}

// Camera-facing circle+cross reticle at a world position — billboarded via
// the camera right/up vectors extracted from the current (translation-
// stripped) modelview matrix, same technique SCRenderer::renderStarfield's
// star-sprite billboards use. White for the selected waypoint, red
// otherwise (navmap.png reference).
void SCNavMap::drawNavMarkerReticle(const Vector3D &worldPos, const Vector3D &color,
                                     float worldSize, const float camMv[16]) {
    float rightX = camMv[0], rightY = camMv[4], rightZ = camMv[8];
    float upX = camMv[1], upY = camMv[5], upZ = camMv[9];

    glDisable(GL_TEXTURE_2D);
    gb.color3f(color.x, color.y, color.z);
    gb.begin(GL_LINES);

    gb.vertex3f(worldPos.x - rightX * worldSize, worldPos.y - rightY * worldSize, worldPos.z - rightZ * worldSize);
    gb.vertex3f(worldPos.x + rightX * worldSize, worldPos.y + rightY * worldSize, worldPos.z + rightZ * worldSize);
    gb.vertex3f(worldPos.x - upX * worldSize, worldPos.y - upY * worldSize, worldPos.z - upZ * worldSize);
    gb.vertex3f(worldPos.x + upX * worldSize, worldPos.y + upY * worldSize, worldPos.z + upZ * worldSize);

    constexpr int kSegments = 16;
    float radius = worldSize * 0.6f;
    for (int i = 0; i < kSegments; i++) {
        float a0 = (2.0f * 3.14159265f) * (float)i / (float)kSegments;
        float a1 = (2.0f * 3.14159265f) * (float)(i + 1) / (float)kSegments;
        float c0 = cosf(a0) * radius, s0 = sinf(a0) * radius;
        float c1 = cosf(a1) * radius, s1 = sinf(a1) * radius;
        gb.vertex3f(worldPos.x + rightX * c0 + upX * s0, worldPos.y + rightY * c0 + upY * s0, worldPos.z + rightZ * c0 + upZ * s0);
        gb.vertex3f(worldPos.x + rightX * c1 + upX * s1, worldPos.y + rightY * c1 + upY * s1, worldPos.z + rightZ * c1 + upZ * s1);
    }
    gb.end();
}

// Billboards the mission's own player-ship icon (MISN_NAV_RES::playerIcon)
// at the player actor's real 3D position. GL texture is built once from the
// icon's indexed RLEShape pixels (index 255 forced transparent, same
// convention as SCCockpit::RenderCockpitOverlayShape) and cached across
// frames keyed off the RSImageSet pointer, rebuilt only if that pointer
// changes (mirrors SCRenderer::buildStarSpriteTextures' own rebuild-only-
// on-change convention).
void SCNavMap::drawPlayerBillboard(RSImageSet* iconSet, const Vector3D &worldPos, const float camMv[16]) {
    if (iconSet == nullptr || iconSet->GetNumImages() == 0) {
        return;
    }
    if (iconSet != this->m_playerIconTextureSource) {
        RLEShape *icon = iconSet->GetShape(0);
        int w = icon->GetWidth();
        int h = icon->GetHeight();
        if (w > 0 && h > 0) {
            std::vector<uint8_t> indexBuf((size_t)w * h, 255);
            icon->buffer_size.x = w;
            icon->buffer_size.y = h;
            size_t byteRead = 0;
            icon->Expand(indexBuf.data(), &byteRead);

            VGAPalette overlayPalette = this->palette;
            overlayPalette.colors[255].a = 0;

            RSImage img;
            img.width = w;
            img.height = h;
            img.data = indexBuf.data();
            img.palette = &overlayPalette;
            img.flags = 0;

            Texture tex;
            tex.set(&img);
            tex.updateContent(&img);
            img.data = nullptr;

            if (this->m_playerIconGLTex == 0) {
                glGenTextures(1, &this->m_playerIconGLTex);
            }
            glBindTexture(GL_TEXTURE_2D, this->m_playerIconGLTex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex.data);
            this->m_playerIconTexW = w;
            this->m_playerIconTexH = h;
        }
        this->m_playerIconTextureSource = iconSet;
    }
    if (this->m_playerIconGLTex == 0 || this->m_playerIconTexW <= 0 || this->m_playerIconTexH <= 0) {
        return;
    }

    float rightX = camMv[0], rightY = camMv[4], rightZ = camMv[8];
    float upX = camMv[1], upY = camMv[5], upZ = camMv[9];
    // World units per icon pixel — tuned so a typical small (~16-24px)
    // marker sprite reads at roughly the same on-screen scale as the
    // navpoint reticles (worldSize = map_width*0.02 in render3DScene, ~7200
    // units for an 18-block map); not a ground-truth value, confirm visually.
    constexpr float kPlayerIconWorldScale = 300.0f;
    float halfW = (float)this->m_playerIconTexW * 0.5f * kPlayerIconWorldScale;
    float halfH = (float)this->m_playerIconTexH * 0.5f * kPlayerIconWorldScale;

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, this->m_playerIconGLTex);
    gb.color4f(1.0f, 1.0f, 1.0f, 1.0f);
    gb.begin(GL_QUADS);
    gb.texCoord2f(0, 1);
    gb.vertex3f(worldPos.x - rightX * halfW - upX * halfH, worldPos.y - rightY * halfW - upY * halfH, worldPos.z - rightZ * halfW - upZ * halfH);
    gb.texCoord2f(1, 1);
    gb.vertex3f(worldPos.x + rightX * halfW - upX * halfH, worldPos.y + rightY * halfW - upY * halfH, worldPos.z + rightZ * halfW - upZ * halfH);
    gb.texCoord2f(1, 0);
    gb.vertex3f(worldPos.x + rightX * halfW + upX * halfH, worldPos.y + rightY * halfW + upY * halfH, worldPos.z + rightZ * halfW + upZ * halfH);
    gb.texCoord2f(0, 0);
    gb.vertex3f(worldPos.x - rightX * halfW + upX * halfH, worldPos.y - rightY * halfW + upY * halfH, worldPos.z - rightZ * halfW + upZ * halfH);
    gb.end();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
}

// Renders the 3D scene (skybox, green grid, navpoint reticles, player-ship
// billboard) into a sub-viewport inset within the existing 2D border art's
// map rectangle (l,t,w,h — VGA-canvas-space, the same box the flat
// background image above occupies). Called from runFrame() right after
// VGA.vSync() so it composites additively on top of the just-presented 2D
// frame (same convention drawWC3TextOverlay already uses — see that hook's
// comment in SCNavMap.h). No color clear: only the inset sub-viewport gets
// new content, the border art around it must stay visible.
//
// Orbit target is the world origin, matching the flat map's own
// normalization (center = BLOCK_WIDTH*9.0f, map_width = BLOCK_WIDTH*18.0f
// in runFrame()) so the 3D view stays spatially consistent with where the
// 2D grid used to be.
void SCNavMap::render3DScene(int l, int t, int w, int h, int canvasW, int canvasH) {
    int vpX, vpY, vpW, vpH;
    this->computeSubViewport(l, t, w, h, canvasW, canvasH, vpX, vpY, vpW, vpH);
    if (vpW <= 0 || vpH <= 0) {
        return;
    }

    float map_width = BLOCK_WIDTH * 18.0f;
    if (this->m_navDistance <= 0.0f) {
        this->m_navDistance = map_width * 0.9f;
    }

    Point3D target{0.0f, 0.0f, 0.0f};
    float cosEl = cosf(this->m_navElevation);
    Point3D camPos{
        target.x + this->m_navDistance * cosEl * sinf(this->m_navAzimuth),
        target.y + this->m_navDistance * sinf(this->m_navElevation),
        target.z + this->m_navDistance * cosEl * cosf(this->m_navAzimuth)
    };

    Renderer.getCamera()->setPersective(45.0f, (float)vpW / (float)vpH, 10.0f, map_width * 4.0f);
    Renderer.getCamera()->SetPosition(&camPos);
    // Camera::lookAt(Point3D*) alone uses a fixed (0,1,0) up vector, which
    // goes parallel to the view direction (degenerate cross product, broken
    // view matrix) exactly at the near-vertical default elevation this map
    // now starts at. Building "up" from azimuth alone instead — a
    // horizontal vector perpendicular to the view's azimuth, well-defined
    // at any elevation including straight down/up — sidesteps the gimbal
    // lock entirely rather than merely clamping short of it.
    Vector3D horizRight(cosf(this->m_navAzimuth), 0.0f, -sinf(this->m_navAzimuth));
    Vector3D forward(target.x - camPos.x, target.y - camPos.y, target.z - camPos.z);
    forward.Normalize();
    Vector3D up = horizRight.CrossProduct(&forward);
    up.Normalize();
    Renderer.getCamera()->lookAt(&target, &up);
    Renderer.getCamera()->update();

    glViewport(vpX, vpY, vpW, vpH);
    glEnable(GL_SCISSOR_TEST);
    glScissor(vpX, vpY, vpW, vpH);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    // Defensive: a previous frame's real 3D flight rendering (models use
    // lighting) may have left GL_LIGHTING enabled — this scene's grid/
    // markers/billboards are all flat-colored/unlit geometry with no
    // normals, same as renderStarfield's own sky sphere, which disables
    // this for the same reason.
    glDisable(GL_LIGHTING);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadMatrixf(Renderer.getCamera()->getProjectionMatrix()->ToGL());
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadMatrixf(Renderer.getCamera()->getViewMatrix()->ToGL());

    // Skybox: mirrors renderWorldSkyAndGround's own show_starfield-vs-
    // skydome branch exactly — WC3 missions set skyFaces/skyEntities (real
    // skybox), SC1 never does (renderSkybox/renderStarfield safely no-op),
    // so this needs no per-game special-casing.
    glDisable(GL_DEPTH_TEST);
    if (Renderer.show_starfield) {
        Renderer.renderStarfield();
        Renderer.renderSkybox();
    } else {
        Renderer.renderSkydome(12, 48);
    }
    glEnable(GL_DEPTH_TEST);

    this->drawNavGrid(map_width * 0.5f, 17);

    float camMv[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, camMv);

    if (this->mission != nullptr) {
        int cpt = 0;
        for (auto wp : this->mission->waypoints) {
            if (wp != nullptr && wp->spot != nullptr && !this->isTakeoffWaypoint(wp)) {
                bool selected = (cpt == (int)*this->current_nav_point);
                // Unselected color sampled directly from the real game
                // (239,56,24) — an orange-red, not pure red.
                Vector3D color = selected ? Vector3D(1.0f, 1.0f, 1.0f)
                                           : Vector3D(239.0f / 255.0f, 56.0f / 255.0f, 24.0f / 255.0f);
                this->drawNavMarkerReticle(wp->spot->position, color, map_width * 0.035f, camMv);
            }
            cpt++;
        }
    }

    // Player-ship icon, from the mission's own NAV chunk (same field the 2D
    // path above uses), billboarded at the player actor's real 3D position
    // instead of flattened to X/Z.
    if (this->missionObj != nullptr && this->mission != nullptr) {
        MISN_NAV_RES &nav = g_ifVGA ? this->missionObj->mission_data.navVGA : this->missionObj->mission_data.navSVGA;
        for (auto friends : this->mission->friendlies) {
            if (friends->actor_name == "PLAYER" && friends->object != nullptr) {
                this->drawPlayerBillboard(nav.playerIcon, friends->object->position, camMv);
                break;
            }
        }
    }

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glViewport(0, 0, VGA.GetWindowWidth(), VGA.GetWindowHeight());
}

// Redraws the same full-canvas border/frame shape runFrame() already drew
// into the FrameBuffer before VGA.vSync() — but as a GL textured quad with
// index 255 forced transparent (same convention as SCCockpit::
// RenderCockpitOverlayShape), on top of render3DScene()'s output. Without
// this, the 3D scene's own geometry (opaque skybox sphere, grid, markers)
// draws over any border-art pixels that fall inside its sub-viewport,
// since it's a later GL pass in the same frame — this re-asserts just the
// border's own opaque pixels on top while its already-transparent interior
// (where the 3D content is meant to show through) is left alone.
void SCNavMap::drawBorderOverlay(RLEShape* borderShape, int canvasW, int canvasH) {
    if (borderShape == nullptr) {
        return;
    }
    int vpX, vpY, vpW, vpH;
    this->computeSubViewport(0, 0, canvasW, canvasH, canvasW, canvasH, vpX, vpY, vpW, vpH);
    if (vpW <= 0 || vpH <= 0) {
        return;
    }

    int w = borderShape->GetWidth();
    int h = borderShape->GetHeight();
    if (w <= 0 || h <= 0) {
        return;
    }
    std::vector<uint8_t> indexBuf((size_t)w * h, 255);
    borderShape->buffer_size.x = w;
    borderShape->buffer_size.y = h;
    size_t byteRead = 0;
    borderShape->Expand(indexBuf.data(), &byteRead);

    VGAPalette overlayPalette = this->palette;
    overlayPalette.colors[255].a = 0;

    RSImage img;
    img.width = w;
    img.height = h;
    img.data = indexBuf.data();
    img.palette = &overlayPalette;
    img.flags = 0;

    Texture tex;
    tex.set(&img);
    tex.updateContent(&img);
    img.data = nullptr;

    glViewport(vpX, vpY, vpW, vpH);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, w, 0, h, -10, 10);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    GLuint glId = 0;
    glGenTextures(1, &glId);
    glBindTexture(GL_TEXTURE_2D, glId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex.data);

    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    gb.color4f(1.0f, 1.0f, 1.0f, 1.0f);

    gb.begin(GL_QUADS);
    gb.texCoord2f(0, 1);
    gb.vertex2d(0, 0);
    gb.texCoord2f(1, 1);
    gb.vertex2d(w, 0);
    gb.texCoord2f(1, 0);
    gb.vertex2d(w, h);
    gb.texCoord2f(0, 0);
    gb.vertex2d(0, h);
    gb.end();

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glDeleteTextures(1, &glId);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glViewport(0, 0, VGA.GetWindowWidth(), VGA.GetWindowHeight());
}

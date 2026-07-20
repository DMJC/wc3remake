//
//  Game.cpp
//  libRealSpace
//
//  Created by Fabien Sanglard on 1/25/2014.
//  Copyright (c) 2014 Fabien Sanglard. All rights reserved.
//

#include "imgui.h"
#include "imgui_impl_opengl2.h"
#include "imgui_impl_sdl2.h"
#include "GameEngine.h"
#include "Loader.h"
#include "InputActionSystem.h"  
#include "EventManager.h"
#include "keyboard.h" 
#include "gametimer.h"
#include <fstream>

GameEngine::GameEngine() {
}

GameEngine::~GameEngine() {
    if (m_keyboard != nullptr) {
        delete m_keyboard;
        m_keyboard = nullptr;
    }
}


void GameEngine::init() {

    // Load Main Palette and Initialize the GL
    Config &config = Config::getInstance();
    int width = config.getInt("Window", "width", 1200);
    int height = config.getInt("Window", "height", 800);
    VGA.init(width,height);
    Renderer.init(width,height);
    // Load the Mouse Cursor
    Mouse.init();
    // Crée le clavier (abstraction)
    this->initKeyboard();

}

void GameEngine::initKeyboard() {
    m_keyboard = new Keyboard();
    Config &config = Config::getInstance();
    std::string bindingsFile = config.getString("Input", "bindings_file", "none");
    if (bindingsFile != "none") {
        std::ifstream f(bindingsFile);
        if (f.good()) {
            f.close();
            m_keyboard->loadActionBindings(bindingsFile);
            return;
        }
    }
    
    // Enregistrer actions souris (position + boutons)
    m_keyboard->registerAction(InputAction::MOUSE_POS_X);
    m_keyboard->registerAction(InputAction::MOUSE_POS_Y);
    m_keyboard->registerAction(InputAction::MOUSE_LEFT);
    m_keyboard->registerAction(InputAction::MOUSE_MIDDLE);
    m_keyboard->registerAction(InputAction::MOUSE_RIGHT);

    // Bind position absolue (axis: 0 = X, 1 = Y)
    m_keyboard->bindMousePositionToAction(InputAction::MOUSE_POS_X, 0, 1.0f);
    m_keyboard->bindMousePositionToAction(InputAction::MOUSE_POS_Y, 1, 1.0f);

    // Bind boutons (indices SDL : left=1, middle=2, right=3)
    m_keyboard->bindMouseButtonToAction(InputAction::MOUSE_LEFT,   SDL_BUTTON_LEFT);
    m_keyboard->bindMouseButtonToAction(InputAction::MOUSE_MIDDLE, SDL_BUTTON_MIDDLE);
    m_keyboard->bindMouseButtonToAction(InputAction::MOUSE_RIGHT,  SDL_BUTTON_RIGHT);

    m_keyboard->registerAction(InputAction::KEY_ESCAPE);
    m_keyboard->bindKeyToAction(InputAction::KEY_ESCAPE, SDL_SCANCODE_ESCAPE);

    // Créer et enregistrer les actions du simulateur
    std::vector<SimActionOfst> allActions = {
        SimActionOfst::THROTTLE_UP,
        SimActionOfst::THROTTLE_DOWN,
        SimActionOfst::THROTTLE_10,
        SimActionOfst::THROTTLE_20,
        SimActionOfst::THROTTLE_30,
        SimActionOfst::THROTTLE_40,
        SimActionOfst::THROTTLE_50,
        SimActionOfst::THROTTLE_60,
        SimActionOfst::THROTTLE_70,
        SimActionOfst::THROTTLE_80,
        SimActionOfst::THROTTLE_90,
        SimActionOfst::THROTTLE_100,
        SimActionOfst::AUTOPILOT,
        SimActionOfst::LOOK_LEFT,
        SimActionOfst::LOOK_RIGHT,
        SimActionOfst::LOOK_BEHIND,
        SimActionOfst::LOOK_FORWARD,
        SimActionOfst::PITCH_UP,
        SimActionOfst::PITCH_DOWN,
        SimActionOfst::ROLL_LEFT,
        SimActionOfst::ROLL_RIGHT,
        SimActionOfst::FIRE_PRIMARY,
        SimActionOfst::TOGGLE_MOUSE,
        SimActionOfst::LANDING_GEAR,
        SimActionOfst::TOGGLE_BRAKES,
        SimActionOfst::TOGGLE_FLAPS,
        SimActionOfst::TARGET_NEAREST,
        SimActionOfst::MDFS_RADAR,
        SimActionOfst::MDFS_DAMAGE,
        SimActionOfst::MDFS_WEAPONS,
        SimActionOfst::SHOW_NAVMAP,
        SimActionOfst::CHAFF,
        SimActionOfst::FLARE,
        SimActionOfst::RADAR_ZOOM_IN,
        SimActionOfst::RADAR_ZOOM_OUT,
        SimActionOfst::COMM_RADIO,
        SimActionOfst::COMM_RADIO_M1,
        SimActionOfst::COMM_RADIO_M2,
        SimActionOfst::COMM_RADIO_M3,
        SimActionOfst::COMM_RADIO_M4,
        SimActionOfst::COMM_RADIO_M5,
        SimActionOfst::VIEW_TARGET,
        SimActionOfst::VIEW_BEHIND,
        SimActionOfst::VIEW_COCKPIT,
        SimActionOfst::VIEW_WEAPONS,
        SimActionOfst::MDFS_TARGET_CAMERA,
        SimActionOfst::PAUSE,
        SimActionOfst::EYES_ON_TARGET,
        SimActionOfst::END_MISSION,
        SimActionOfst::MOUSE_X,
        SimActionOfst::MOUSE_Y,
        SimActionOfst::COMM_RADIO_M6,
        SimActionOfst::COMM_RADIO_M7,
        SimActionOfst::COMM_RADIO_M8,
        SimActionOfst::CONTROLLER_STICK_LEFT_X,
        SimActionOfst::CONTROLLER_STICK_LEFT_Y,
        SimActionOfst::CONTROLLER_STICK_RIGHT_X,
        SimActionOfst::CONTROLLER_STICK_RIGHT_Y,
        SimActionOfst::RADAR_MODE_TOGGLE,
        SimActionOfst::RUDDER_LEFT,
        SimActionOfst::RUDDER_RIGHT,
        SimActionOfst::THROTTLE_STOP,
        SimActionOfst::AFTERBURNER,
        SimActionOfst::MODIFIER_SHIFT,
        SimActionOfst::FIRE_MISSILE,
        SimActionOfst::CYCLE_GUNS,
        SimActionOfst::VIEW_TRACK
    };
    
    for (auto action : allActions) {
        m_keyboard->registerAction(CreateAction(InputAction::SIM_START, action));
    }
    // Association des touches aux actions
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_UP), SDL_SCANCODE_EQUALS);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_DOWN), SDL_SCANCODE_MINUS);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_10), SDL_SCANCODE_1);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_20), SDL_SCANCODE_2);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_30), SDL_SCANCODE_3);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_40), SDL_SCANCODE_4);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_50), SDL_SCANCODE_5);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_60), SDL_SCANCODE_6);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_70), SDL_SCANCODE_7);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_80), SDL_SCANCODE_8);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_90), SDL_SCANCODE_9);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_100), SDL_SCANCODE_0);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::AUTOPILOT), SDL_SCANCODE_A);
    // F1 moved to CYCLE_COCKPIT_VIEW; LOOK_FORWARD relocated to F12 (the one
    // free function key) to make room.
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::CYCLE_COCKPIT_VIEW), SDL_SCANCODE_F1);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::LOOK_FORWARD), SDL_SCANCODE_F12);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::LOOK_LEFT), SDL_SCANCODE_F2);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::LOOK_RIGHT), SDL_SCANCODE_F3);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::LOOK_BEHIND), SDL_SCANCODE_F4);
    // Down arrow = Pitch Up, Up arrow = Pitch Down — matches WC3's real
    // (joystick-pull-back-style) control scheme per live confirmation,
    // not the naive up=up mapping.
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::PITCH_UP), SDL_SCANCODE_DOWN);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::PITCH_DOWN), SDL_SCANCODE_UP);
    // Left/Right arrows = Yaw (not Roll) — Roll lives on comma/period below.
    // Swapped from the naive left=LEFT/right=RIGHT mapping per live
    // testing feedback (yaw direction itself confirmed correct once the
    // world-relative-yaw fix landed, but left/right were backwards).
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::RUDDER_LEFT), SDL_SCANCODE_RIGHT);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::RUDDER_RIGHT), SDL_SCANCODE_LEFT);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::FIRE_PRIMARY), SDL_SCANCODE_SPACE);
    // Guns on Space, missiles/torpedo on Enter — real WC-series controls,
    // not a single fire button (see FIRE_MISSILE's own comment).
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::FIRE_MISSILE), SDL_SCANCODE_RETURN);
    // Moved off M — M is now bound to MDFS_WEAPONS below (real WC3 control:
    // G cycles gun type, M cycles missile/ordnance hardpoint).
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::TOGGLE_MOUSE), SDL_SCANCODE_U);
    // M cycles which missile/ordnance hardpoint FIRE_MISSILE/Enter fires —
    // same action W already triggers (MDFS_WEAPONS' cycling handler,
    // SCStrike.cpp, updates selected_weapon); bound additively, not a
    // replacement for W.
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::MDFS_WEAPONS), SDL_SCANCODE_M);
    // LANDING_GEAR moved off L to O so L is free for LOCK_TARGET (real WC3
    // uses L for target lock).
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::LANDING_GEAR), SDL_SCANCODE_O);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::LOCK_TARGET), SDL_SCANCODE_L);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::TOGGLE_BRAKES), SDL_SCANCODE_B);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::TOGGLE_FLAPS), SDL_SCANCODE_F);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::TARGET_NEAREST), SDL_SCANCODE_T);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::MDFS_RADAR), SDL_SCANCODE_R);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::MDFS_DAMAGE), SDL_SCANCODE_D);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::MDFS_WEAPONS), SDL_SCANCODE_W);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::SHOW_NAVMAP), SDL_SCANCODE_N);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::CHAFF), SDL_SCANCODE_SEMICOLON);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::FLARE),SDL_SCANCODE_APOSTROPHE);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::RADAR_ZOOM_IN), SDL_SCANCODE_LEFTBRACKET);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::RADAR_ZOOM_OUT), SDL_SCANCODE_RIGHTBRACKET);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::COMM_RADIO), SDL_SCANCODE_C);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::COMM_RADIO_M1), SDL_SCANCODE_1);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::COMM_RADIO_M2), SDL_SCANCODE_2);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::COMM_RADIO_M3), SDL_SCANCODE_3);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::COMM_RADIO_M4), SDL_SCANCODE_4);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::COMM_RADIO_M5), SDL_SCANCODE_5);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::VIEW_TARGET), SDL_SCANCODE_F7);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::VIEW_BEHIND), SDL_SCANCODE_F5);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::VIEW_COCKPIT), SDL_SCANCODE_F6);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::VIEW_WEAPONS), SDL_SCANCODE_F8);
    // Real WC3 F9 is WRLD>CAMR>VCTC, "Victim Camera" (user-confirmed
    // 2026-07 session) — this action predates knowing that real name, but
    // its behavior (an inset MFD panel showing the current target/victim,
    // SCCockpit::show_cam) already matches: not a full camera_mode switch
    // like CHAS/WEAP/TRAK, just a picture-in-picture toggle, which is
    // exactly the shape real WC3's F9 has. Left under its existing name to
    // avoid an unnecessary rename across every call site.
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::MDFS_TARGET_CAMERA), SDL_SCANCODE_F9);
    // Real WC3 F10 is WRLD>CAMR>TRAK, "Track Camera" (user-confirmed
    // 2026-07 session) — moved SPEC_KEY_1 (a mission-force-end debug cheat,
    // not a real WC3 control at all) off F10 onto X to make room.
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::VIEW_TRACK), SDL_SCANCODE_F10);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::SPEC_KEY_1), SDL_SCANCODE_X);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::SPEC_KEY_2), SDL_SCANCODE_F11);
    // PAUSE moved off P so P is free for MDFS_POWER (left MFD power meters).
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::PAUSE), SDL_SCANCODE_E);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::MDFS_POWER), SDL_SCANCODE_P);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::MDFS_SHIELD), SDL_SCANCODE_S);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::EYES_ON_TARGET), SDL_SCANCODE_Y);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::END_MISSION), SDL_SCANCODE_ESCAPE);
    // Comma/period = Roll (not rudder/yaw) — see PITCH_UP/RUDDER_LEFT
    // bindings above for the arrow-key remap this pairs with.
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::ROLL_LEFT), SDL_SCANCODE_COMMA);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::ROLL_RIGHT), SDL_SCANCODE_PERIOD);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::MODIFIER_SHIFT), SDL_SCANCODE_LSHIFT);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::MODIFIER_SHIFT), SDL_SCANCODE_RSHIFT);
    m_keyboard->bindMousePositionToAction(CreateAction(InputAction::SIM_START, SimActionOfst::MOUSE_X), 0, 1.0f);
    m_keyboard->bindMousePositionToAction(CreateAction(InputAction::SIM_START, SimActionOfst::MOUSE_Y), 1, 1.0f);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::COMM_RADIO_M6),SDL_SCANCODE_6);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::COMM_RADIO_M7),SDL_SCANCODE_7);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::COMM_RADIO_M8),SDL_SCANCODE_8);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::RADAR_MODE_TOGGLE), SDL_SCANCODE_V);
    // Moved off G — G is now CYCLE_GUNS (real WC3 gun-type-select control,
    // see CYCLE_GUNS's own comment).
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::WEAPON_MODE_TOGGLE), SDL_SCANCODE_H);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::CYCLE_GUNS), SDL_SCANCODE_G);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::INFINIT_AMMO_TOGGLE), SDL_SCANCODE_I);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::SINGLE_TARGET_MODE), SDL_SCANCODE_K);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_STOP), SDL_SCANCODE_BACKSPACE);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_100), SDL_SCANCODE_BACKSLASH);
    m_keyboard->bindKeyToAction(CreateAction(InputAction::SIM_START, SimActionOfst::AFTERBURNER), SDL_SCANCODE_TAB);

    m_keyboard->bindGamepadButtonToAction(CreateAction(InputAction::SIM_START, SimActionOfst::FIRE_PRIMARY), 0, SDL_CONTROLLER_BUTTON_A);
    // Real HOTAS/flight-stick trigger convention: button 1 = guns, button 2
    // = missiles — SDL joystick buttons are 0-indexed internally, so
    // "button 1"/"button 2" map to indices 0/1 here. Separate from the
    // SDL_CONTROLLER_BUTTON_* gamepad bindings above/below, which target
    // Xbox/PlayStation-style pads via SDL's GameController abstraction
    // rather than a raw joystick device.
    m_keyboard->bindJoystickButtonToAction(CreateAction(InputAction::SIM_START, SimActionOfst::FIRE_PRIMARY), 0, 0);
    m_keyboard->bindJoystickButtonToAction(CreateAction(InputAction::SIM_START, SimActionOfst::FIRE_MISSILE), 0, 1);
    m_keyboard->bindGamepadButtonToAction(CreateAction(InputAction::SIM_START, SimActionOfst::TARGET_NEAREST), 0, SDL_CONTROLLER_BUTTON_B);
    m_keyboard->bindGamepadButtonToAction(CreateAction(InputAction::SIM_START, SimActionOfst::MDFS_RADAR), 0, SDL_CONTROLLER_BUTTON_X);
    m_keyboard->bindGamepadButtonToAction(CreateAction(InputAction::SIM_START, SimActionOfst::MDFS_WEAPONS), 0, SDL_CONTROLLER_BUTTON_Y);
    m_keyboard->bindGamepadButtonToAction(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_DOWN), 0, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    m_keyboard->bindGamepadButtonToAction(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_UP), 0, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    m_keyboard->bindGamepadButtonToAction(CreateAction(InputAction::SIM_START, SimActionOfst::TOGGLE_BRAKES), 0, SDL_CONTROLLER_BUTTON_DPAD_UP);
    m_keyboard->bindGamepadButtonToAction(CreateAction(InputAction::SIM_START, SimActionOfst::TOGGLE_FLAPS), 0, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    m_keyboard->bindGamepadButtonToAction(CreateAction(InputAction::SIM_START, SimActionOfst::LANDING_GEAR), 0, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    m_keyboard->bindGamepadButtonToAction(CreateAction(InputAction::SIM_START, SimActionOfst::AUTOPILOT), 0, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    m_keyboard->bindGamepadButtonToAction(CreateAction(InputAction::SIM_START, SimActionOfst::PAUSE), 0, SDL_CONTROLLER_BUTTON_START);
    m_keyboard->bindGamepadButtonToAction(CreateAction(InputAction::SIM_START, SimActionOfst::SHOW_NAVMAP), 0, SDL_CONTROLLER_BUTTON_BACK);

    m_keyboard->bindGamepadAxisToAction(CreateAction(InputAction::SIM_START, SimActionOfst::CONTROLLER_STICK_LEFT_X), 0, SDL_CONTROLLER_AXIS_LEFTX, 1.0f);
    m_keyboard->bindGamepadAxisToAction(CreateAction(InputAction::SIM_START, SimActionOfst::CONTROLLER_STICK_LEFT_Y), 0, SDL_CONTROLLER_AXIS_LEFTY, -1.0f);

    m_keyboard->bindGamepadAxisToAction(CreateAction(InputAction::SIM_START, SimActionOfst::CONTROLLER_STICK_RIGHT_X), 0, SDL_CONTROLLER_AXIS_RIGHTX, 1.0f);
    m_keyboard->bindGamepadAxisToAction(CreateAction(InputAction::SIM_START, SimActionOfst::CONTROLLER_STICK_RIGHT_Y), 0, SDL_CONTROLLER_AXIS_RIGHTY, -1.0f);

    m_keyboard->bindGamepadAxisToAction(InputAction::CONTROLLER_STICK_LEFT_X, 0, SDL_CONTROLLER_AXIS_LEFTX, 1.0f);
    m_keyboard->bindGamepadAxisToAction(InputAction::CONTROLLER_STICK_LEFT_Y, 0, SDL_CONTROLLER_AXIS_LEFTY, -1.0f);
    m_keyboard->bindGamepadButtonToAction(InputAction::CONTROLLER_BUTTON_A, 0, SDL_CONTROLLER_BUTTON_A);
    m_keyboard->bindGamepadButtonToAction(InputAction::CONTROLLER_BUTTON_B, 0, SDL_CONTROLLER_BUTTON_B);
    m_keyboard->bindGamepadButtonToAction(InputAction::CONTROLLER_BUTTON_BACK, 0, SDL_CONTROLLER_BUTTON_BACK);
    m_keyboard->bindGamepadButtonToAction(InputAction::CONTROLLER_BUTTON_START, 0, SDL_CONTROLLER_BUTTON_START);
    
    m_keyboard->bindMouseAxisToAction(InputAction::MOUSE_DIFF_X, 0, 1.0f);
    m_keyboard->bindMouseAxisToAction(InputAction::MOUSE_DIFF_Y, 1, 1.0f);
    m_keyboard->saveActionBindings("default_bindings.cfg");
}

void GameEngine::pumpEvents(void) {

    // Met à jour tout (Keyboard encapsule InputActionSystem/EventManager)
    m_keyboard->update();

    // Position absolue (pixels fenêtre)
    int px, py;
    m_keyboard->getMouseAbsolutePosition(px, py);

    float mx = m_keyboard->getActionValue(InputAction::MOUSE_DIFF_X);
    float my = m_keyboard->getActionValue(InputAction::MOUSE_DIFF_Y);
    
    float cx = m_keyboard->getActionValue(InputAction::CONTROLLER_STICK_LEFT_X);
    float cy = m_keyboard->getActionValue(InputAction::CONTROLLER_STICK_LEFT_Y);

    
    // Conversion vers l’espace 320x200 legacy
    Point2D newPosition;
    if (cx != 0 || cy != 0) {
        lastControllerPosition.x += cx * 4;
        lastControllerPosition.y -= cy * 4;
    } else if (mx != 0 || my != 0) {
        if (direct_mouse_control) {
            lastControllerPosition.x = (px * 320) / Screen->logical_width;
            lastControllerPosition.y = (py * 200) / Screen->logical_height;
        } else {
            lastControllerPosition.x += mx;
            lastControllerPosition.y += my;
        }
        
    }
    if (lastControllerPosition.x < 0) lastControllerPosition.x = 0;
    if (lastControllerPosition.y < 0) lastControllerPosition.y = 0;
    if (lastControllerPosition.x > 320) lastControllerPosition.x = 320;
    if (lastControllerPosition.y > 200) lastControllerPosition.y = 200; 
    newPosition.x = lastControllerPosition.x;
    newPosition.y = lastControllerPosition.y;
    Mouse.setPosition(newPosition);
    Mouse.setVisible(true);
    // --- Fusion souris + contrôleur sur les 3 boutons legacy (0:Left,1:Middle,2:Right)
    // On combine les états "down" de chaque source puis on émet UNE transition.
    static bool prevDown[3] = { false, false, false };

    auto isDown = [&](InputAction a) -> bool {
        // Hypothèse: getActionValue(a) retourne 1.0f si enfoncé, 0.0f sinon.
        // Si ce n'est pas le cas, exposez un isActionDown(a) et utilisez-le ici.
        return m_keyboard->getActionValue(a) > 0.5f;
    };

    bool currentDown[3];
    currentDown[0] = isDown(InputAction::MOUSE_LEFT)   || isDown(InputAction::CONTROLLER_BUTTON_A);
    currentDown[1] = isDown(InputAction::MOUSE_MIDDLE) || isDown(InputAction::CONTROLLER_BUTTON_B);
    currentDown[2] = isDown(InputAction::MOUSE_RIGHT); // ajoutez ici d'autres boutons pad si besoin

    for (int i = 0; i < 3; ++i) {
        MouseButton::EventType e = MouseButton::NONE;
        if ( currentDown[i] && !prevDown[i] ) {
            e = MouseButton::PRESSED;
        } else if ( !currentDown[i] && prevDown[i] ) {
            e = MouseButton::RELEASED;
        }
        Mouse.buttons[i].event = e;
        prevDown[i] = currentDown[i];
    }

    if (EventManager::getInstance().shouldQuit()) {
        terminate("System request.");
        return;
    }
}

void GameEngine::run() {

    IActivity *currentActivity;
    while (activities.size() > 0) {
        GameTimer::getInstance().update();
        pumpEvents();

        if (m_globalHotkeyCheck) m_globalHotkeyCheck();

        currentActivity = activities.top();
        if (currentActivity->isRunning()) {
            currentActivity->focus();
            currentActivity->runFrame();
        } else {
            activities.pop();
            delete currentActivity;
        }

        Screen->refresh();

        Mouse.flushEvents(); // On peut le garder si sa logique interne reste valable.
    }
}

void GameEngine::terminate(const char *reason, ...) {
    log("Terminating: ");
    va_list args;
    va_start(args, reason);
    vfprintf(stdout, reason, args);
    va_end(args);
    log("\n");
    exit(0);
}

void GameEngine::log(const char *text, ...) {
    va_list args;
    va_start(args, text);
    vfprintf(stdout, text, args);
    va_end(args);
}

void GameEngine::logError(const char *text, ...) {
    va_list args;
    va_start(args, text);
    vfprintf(stderr, text, args);
    va_end(args);
}

void GameEngine::addActivity(IActivity *activity) {
    if (activities.size()>0) {
        IActivity *currentActivity;
        currentActivity = activities.top();
        currentActivity->unFocus();
    }
    activity->start();
    this->activities.push(activity);
}

void GameEngine::stopTopActivity(void) {
    IActivity *currentActivity;
    currentActivity = activities.top();
    currentActivity->stop();
}

IActivity *GameEngine::getCurrentActivity(void) { return activities.top(); }

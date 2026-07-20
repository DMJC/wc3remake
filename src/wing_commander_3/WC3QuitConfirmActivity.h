#pragma once
#include "../engine/IActivity.h"

// Minimal "Quit game? Y/N" confirmation overlay, pushed on top of whatever
// flight activity (WC3Strike) is currently running when the player presses
// Alt+X — mirrors WC3OptionsActivity's own pattern (self-contained RGBA
// buffer + WC3Font, no WC3Scene/shape-pak dependency, since it must work
// mid-flight where no WC3Scene exists) rather than reusing WC3GameFlow's
// AWAITING_BRANCH_CHOICE dialogue-branch UI, which is tied to WC3GameFlow's
// own state machine and isn't reachable during flight.
//
// Solid full-screen background, not a transparent overlay over the flight
// scene: while this activity sits on top of the GameEngine activity stack,
// GameEngine::run() only calls runFrame() on the top activity each frame
// (see the main loop), so WC3Strike's own 3D scene simply doesn't get
// redrawn while this is up — there's no "behind" to show through.
class WC3QuitConfirmActivity : public IActivity {
public:
    void init() override;
    void runFrame() override;
};

#pragma once

// Global VGA (320x200) vs SVGA (640x480) resolution-mode flag. Lives in
// commons/ — the lowest layer, already reachable from realspace up through
// wing_commander_3 — specifically so low-layer code (e.g. RSCockpit's own
// choice between targetHudVGA/targetHudSVGA, RSMission's navVGA/navSVGA)
// can read it directly, without realspace needing to depend on engine's
// Config class or wing_commander_3's WC3Options (the actual persisted,
// user-facing setting — see WC3Options.h's own if_VGA field and
// wc3_options.cfg load/save — both of which just keep this in sync).
//
// Defaults to SVGA (false) per explicit instruction: "set this variable to
// 0 by default."
extern bool g_ifVGA;

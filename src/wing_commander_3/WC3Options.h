#pragma once
#include "../engine/Config.hpp"
#include "../commons/GraphicsSettings.h"

// Player-configurable settings, backed by the game's own config.ini
// ([Options] section) — read once at startup (creating the file with these
// defaults if it doesn't exist yet, see main.cpp), edited via
// WC3OptionsActivity, and saved back whenever a setting changes.
//
// Only music/sound volume and transitionsOn have a real effect on engine
// behavior today (RSMixer::setVolume and the movie-playback gate,
// respectively) — gamma, subtitle language, and the stars/descriptions
// toggles are persisted and toggleable but there is no real
// gamma/localization system to hook them to yet. That's stated here plainly
// rather than faked.
//
// VGA/SVGA mode is deliberately NOT a field on this struct. It's the single
// global g_ifVGA flag (commons/GraphicsSettings.h) — a true radio choice,
// not two independent gameflow/movies settings like before — persisted to
// its own dedicated wc3_options.cfg file rather than config.ini. See
// loadGraphicsSettings/saveGraphicsSettings below.
struct WC3Options {
    bool soundEnabled{true};
    bool musicEnabled{true};
    int  soundVolume{128}; // SDL_mixer native range (MIX_MAX_VOLUME), see RSMixer::setVolume
    int  musicVolume{128};

    enum class Gamma { Off, Low, High };
    Gamma gamma{Gamma::Off};

    bool subtitlesOn{true};
    enum class Language { English, French, German };
    Language language{Language::English};

    bool transitionsOn{true}; // replaces the old opt_transition_videos bool — real: gates movie playback
    bool starsOn{true};
    bool descriptionsOn{true};

    // Dev-only, no in-game checkbox — replaces the old opt_stub_missions
    // bool. Rides along in the same struct/config file so there's one load
    // path and one save path instead of two parallel settings mechanisms.
    bool devStubMissions{false};
};

inline void loadOptions(WC3Options& o, Config& config) {
    o.soundEnabled    = config.getBool("Options", "sound_enabled", o.soundEnabled);
    o.musicEnabled    = config.getBool("Options", "music_enabled", o.musicEnabled);
    o.soundVolume     = config.getInt("Options", "sound_volume", o.soundVolume);
    o.musicVolume     = config.getInt("Options", "music_volume", o.musicVolume);
    o.gamma           = (WC3Options::Gamma)config.getInt("Options", "gamma", (int)o.gamma);
    o.subtitlesOn     = config.getBool("Options", "subtitles_on", o.subtitlesOn);
    o.language        = (WC3Options::Language)config.getInt("Options", "language", (int)o.language);
    o.transitionsOn   = config.getBool("Options", "transitions_on", o.transitionsOn);
    o.starsOn         = config.getBool("Options", "stars_on", o.starsOn);
    o.descriptionsOn  = config.getBool("Options", "descriptions_on", o.descriptionsOn);
    o.devStubMissions = config.getBool("Options", "dev_stub_missions", o.devStubMissions);
}

inline void saveOptions(const WC3Options& o, Config& config) {
    config.setBool("Options", "sound_enabled", o.soundEnabled);
    config.setBool("Options", "music_enabled", o.musicEnabled);
    config.setInt("Options", "sound_volume", o.soundVolume);
    config.setInt("Options", "music_volume", o.musicVolume);
    config.setInt("Options", "gamma", (int)o.gamma);
    config.setBool("Options", "subtitles_on", o.subtitlesOn);
    config.setInt("Options", "language", (int)o.language);
    config.setBool("Options", "transitions_on", o.transitionsOn);
    config.setBool("Options", "stars_on", o.starsOn);
    config.setBool("Options", "descriptions_on", o.descriptionsOn);
    config.setBool("Options", "dev_stub_missions", o.devStubMissions);
}

// Dedicated path for the VGA/SVGA choice — separate from config.ini so it
// can be read as early as possible at startup (before RSVGA::init() sizes
// the canvas, see main.cpp), without needing the rest of the config.ini
// [Options] section or its Config::getInstance() singleton set up first.
inline constexpr const char* kWC3GraphicsConfigPath = "./assets/wc3_options.cfg";

// Uses its own local Config instance rather than Config::getInstance() —
// this file is intentionally separate from config.ini, so it gets its own
// separate in-memory CSimpleIniA rather than sharing/clobbering the main
// config's. No-ops (leaves g_ifVGA at its current value) if the file
// doesn't exist yet, e.g. before the player has ever touched the options
// screen — g_ifVGA's own default (false, SVGA) then applies.
inline void loadGraphicsSettings(const std::string& filePath = kWC3GraphicsConfigPath) {
    Config gfxConfig;
    if (gfxConfig.load(filePath)) {
        g_ifVGA = gfxConfig.getBool("Graphics", "if_VGA", g_ifVGA);
    }
}

inline void saveGraphicsSettings(const std::string& filePath = kWC3GraphicsConfigPath) {
    Config gfxConfig;
    gfxConfig.load(filePath); // preserve the file if it already exists; harmless no-op otherwise
    gfxConfig.setBool("Graphics", "if_VGA", g_ifVGA);
    gfxConfig.save(filePath);
}

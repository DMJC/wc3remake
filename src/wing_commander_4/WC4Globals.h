#pragma once
#include "../strike_commander/precomp.h"
#include "../realspace/PakArchive.h"
#include "../realspace/RSImageSet.h"
#include "WC4Font.h"
#include <unordered_map>

// Parses globals.iff (FORM:GLOB) — the game's single bootstrap config file.
// Contains the PATH virtual-filesystem table, 9 bitmap fonts, 10 cursor
// shapes, the simulation tick rate, and display-driver hints.
class WC4Globals {
public:
    static WC4Globals& getInstance() {
        static WC4Globals inst;
        return inst;
    }

    bool load(const uint8_t* data, size_t size);
    bool isLoaded() const { return loaded; }

    // FORM:PATH — asset-type tag → DOS-relative directory path.
    // Tags: PALT WRLD OBJS BKGD COCK WEAP TERA TXMS FLOW MOVI INFL PROF PROS INTL SOND SFOP
    std::string getPath(const char* tag) const;

    // FORM:FONT — bitmap fonts by IFF chunk name.
    // Names: VSML VLRG SSML SLRG SUBS SUBV GFLG GFMD GFSM
    WC4Font* getFont(const char* name) const;

    // FORM:PNTR — cursor shapes (10 cursors, 30×31 px).
    // Returns null if not yet loaded.
    RSImageSet* getCursors() const { return cursorSet; }

    // FORM:WRLD/TICK — 16.16 fixed-point ticks/second (== 120.0).
    float getTickRate() const { return tickRate; }

private:
    WC4Globals() = default;
    WC4Globals(const WC4Globals&) = delete;

    bool loaded{false};
    float tickRate{120.0f};

    std::unordered_map<std::string, std::string> paths;
    std::unordered_map<std::string, WC4Font*>    fonts;

    PakArchive   cursorPak;
    RSImageSet*  cursorSet{nullptr};

    static uint32_t r32be(const uint8_t* p);
    static uint32_t r32le(const uint8_t* p);

    void parsePath(const uint8_t* data, size_t size);
    void parseFont(const uint8_t* data, size_t size);
    void parseWrld(const uint8_t* data, size_t size);
    void parsePntr(const uint8_t* data, size_t size);
};

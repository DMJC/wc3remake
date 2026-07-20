#include "WC3NavMap.h"
#include "WC3Globals.h"
#include "WC3Font.h"
#include "../commons/GraphicsSettings.h"
#include <cstdio>
#include <cmath>

// Finds the closest VGAPalette entry to an RGB color (simple squared
// distance) — WC3Font renders real RGBA pixels, but this screen still
// draws everything else through the palette-indexed FrameBuffer (see
// SCNavMap::drawWC3TextOverlay's own comment for why: keeping text inside
// the same FrameBuffer→VGA.vSync() pipeline avoids having to duplicate
// RSVGA::displayBuffer's own aspect-corrected viewport/ortho math in a
// second, separately-aligned GL pass).
static uint8_t nearestPaletteIndex(VGAPalette &pal, uint8_t r, uint8_t g, uint8_t b) {
    int bestIdx = 0;
    int bestDist = 1 << 30;
    for (int i = 0; i < 256; i++) {
        const Texel *c = pal.GetRGBColor(i);
        int dr = (int)c->r - r, dg = (int)c->g - g, db = (int)c->b - b;
        int dist = dr * dr + dg * dg + db * db;
        if (dist < bestDist) {
            bestDist = dist;
            bestIdx = i;
        }
    }
    return (uint8_t)bestIdx;
}

// Renders one line of WC3Font text into a small local RGBA buffer, then
// stamps every non-transparent pixel into the VGA FrameBuffer (palette-
// indexed, nearest-match per WC3Font's own rendered color) at (x, y). color
// is the same r|(g<<8)|(b<<16)|(a<<24) format WC3Font::drawTextRGBA takes —
// defaults to its own default (opaque white).
static void stampTextLine(FrameBuffer *fb, VGAPalette &pal, WC3Font *font,
                           const std::string &text, int x, int y, uint32_t color = 0xFFFFFFFF) {
    if (text.empty()) return;
    int w = font->measureText(text);
    int h = font->getHeight();
    if (w <= 0 || h <= 0) return;
    std::vector<uint32_t> rgba((size_t)w * h, 0);
    font->drawTextRGBA(rgba.data(), w, h, text, 0, 0, color);
    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            uint32_t px_val = rgba[(size_t)py * w + px];
            uint8_t alpha = (px_val >> 24) & 0xFF;
            if (alpha < 64) continue;
            uint8_t r = px_val & 0xFF, g = (px_val >> 8) & 0xFF, b = (px_val >> 16) & 0xFF;
            int destX = x + px, destY = y + py;
            if (destX < 0 || destX >= fb->width || destY < 0 || destY >= fb->height) continue;
            fb->plot_pixel(destX, destY, nearestPaletteIndex(pal, r, g, b));
        }
    }
}

// Substitutes the first "%s" in a printf-style format string (these come
// straight from MISN>NAV>VGA>TEXT/OBJT etc. — see RSMission.h's
// MISN_NAV_RES comment) with a plain value — not real printf, since these
// are untrusted-ish mission-authored strings and the substitution is
// always exactly one %s.
static std::string substitutePct_s(const std::string &fmt, const std::string &value) {
    size_t pos = fmt.find("%s");
    if (pos == std::string::npos) return fmt;
    return fmt.substr(0, pos) + value + fmt.substr(pos + 2);
}

void WC3NavMap::drawWC3TextOverlay(float center, float map_width, int w, int h, int t, int l) {
    if (this->missionObj == nullptr || this->mission == nullptr) {
        return;
    }
    WC3Font *font = WC3Globals::getInstance().getFont("GFSM");
    if (font == nullptr || !font->isLoaded()) {
        return;
    }
    MISN_NAV_RES &nav = g_ifVGA ? this->missionObj->mission_data.navVGA : this->missionObj->mission_data.navSVGA;
    if (nav.textLines_en.size() < 4) {
        return;
    }
    FrameBuffer *fb = VGA.getFrameBuffer();

    // Top banner spanning the full width of the map box (navmap.png
    // reference) — was a right-side sidebar column before (one line per
    // call, stacked outside the map area), which didn't match the real
    // game's layout at all.
    int lineH = font->getHeight() + 3;
    int y = t + 2;
    int textX = l + 4;

    // Line 1, two columns: mission name (left) / "NAV AREA:<name>" (right,
    // green). mission_data.world_filename (an internal id like "wrlda1")
    // used to be substituted here instead of mission_data.name — the real
    // display name WC3 itself shows, parsed from the MISN>NAME chunk (see
    // RSMission::parseMISN_NAME).
    std::string missionName = this->mission->mission->mission_data.name;
    stampTextLine(fb, this->palette, font, substitutePct_s(nav.textLines_en[1], missionName), textX, y);

    SCMissionWaypoint *wp = nullptr;
    if (this->current_nav_point != nullptr && *this->current_nav_point < this->mission->waypoints.size()) {
        wp = this->mission->waypoints[*this->current_nav_point];
    }
    // "NAV AREA:" has no printf-style template in MISN>NAV>TEXT (only the
    // Mission/Objective/Notes %s templates live there) — a literal label
    // styled after navmap.png, not mission-authored data. AREA::AreaName
    // (confirmed by direct user knowledge of the format) holds the
    // "SWEEP"-style area name for the waypoint's linked AREA chunk.
    if (wp != nullptr && wp->spot != nullptr && wp->spot->area_id >= 0 &&
        wp->spot->area_id < (int)this->missionObj->mission_data.areas.size()) {
        AREA *area = this->missionObj->mission_data.areas[wp->spot->area_id];
        std::string areaLine = std::string("NAV AREA:") + area->AreaName;
        int areaLineW = font->measureText(areaLine);
        int areaX = l + w - 4 - areaLineW;
        stampTextLine(fb, this->palette, font, areaLine, areaX, y, 0xFF00FF00);
    }
    y += lineH;

    if (wp != nullptr) {
        if (wp->objective != nullptr) {
            stampTextLine(fb, this->palette, font, substitutePct_s(nav.textLines_en[2], *wp->objective), textX, y);
        }
        y += lineH;
        if (wp->message != nullptr) {
            stampTextLine(fb, this->palette, font, substitutePct_s(nav.textLines_en[3], *wp->message), textX, y);
        }
    }
}

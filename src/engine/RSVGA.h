//
//  VGA.h
//  libRealSpace
//
//  Created by Fabien Sanglard on 1/27/2014.
//  Copyright (c) 2014 Fabien Sanglard. All rights reserved.
//

#pragma once
#ifdef _WIN32
#include <Windows.h>
#endif
#ifndef __APPLE__
    #include <GL/gl.h>
#else
    #define GL_SILENCE_DEPRECATION
    #include <OpenGL/gl.h>
#endif

#include "../realspace/AssetManager.h"
#include "../realspace/RLEShape.h"
#include "../realspace/RSFont.h"
#include "RSScreen.h"
#include "Texture.h"
#include "FrameBuffer.h"

#define sgn(x) ((x<0)?-1:((x>0)?1:0))

struct VGAPalette;

class RSVGA{
private:
    int width;
    int height;
    VGAPalette palette;
    FrameBuffer* frameBuffer;
    uint32_t *upscaled_framebuffer{nullptr};
    uint32_t textureID;
    // Was a fixed Texel data[320*200] — the logical 2D-screen canvas
    // (nav map, cockpit HUD, targeting-box overlay, ...) used to be
    // permanently VGA (320x200) regardless of the if_VGA setting (see
    // commons/GraphicsSettings.h), since this buffer's size was baked into
    // the class itself rather than being configurable. Heap-allocated and
    // resized via SetCanvasResolution() instead, so it can genuinely be
    // 640x480 for SVGA mode.
    Texel *data{nullptr};
    int canvasWidth{320};
    int canvasHeight{200};
    inline static std::unique_ptr<RSVGA> s_instance{};
    void displayBuffer(uint32_t* buffer, int width, int height);
    RSScreen *Screen = &RSScreen::instance();
public:
    bool upscale{false};
    // (Re)allocates frameBuffer/data/upscaled_framebuffer and the backing
    // GL texture at the given logical canvas resolution — called once from
    // init() using the current g_ifVGA setting, and again whenever the
    // player changes the VGA/SVGA option so it takes effect without a
    // restart. Safe to call with the same size repeatedly (no-ops the
    // reallocation) since options-screen clicks call it every frame the
    // setting is active, not just on change.
    void SetCanvasResolution(int w, int h);
    int GetCanvasWidth() const { return canvasWidth; }
    int GetCanvasHeight() const { return canvasHeight; }
    
    static RSVGA& getInstance() {
        if (!RSVGA::hasInstance()) {
            RSVGA::setInstance(std::make_unique<RSVGA>());
        }
        RSVGA& instance = RSVGA::instance();
        return instance;
    };    
    static RSVGA& instance() {
        return *s_instance; 
    }
    static void setInstance(std::unique_ptr<RSVGA> inst) {
        s_instance = std::move(inst);
    }
    static bool hasInstance() { return (bool)s_instance; }

    RSVGA();
    ~RSVGA();
    void init(int width, int height);
    void activate(void);
    void setPalette(VGAPalette* newPalette);
    VGAPalette* getPalette(void);
    void vSync(void);
    void fadeOut(int steps = 10, int delayMs = 50);
    FrameBuffer* getFrameBuffer(void){ return frameBuffer;};
    // Actual window/output pixel size (the letterboxed-to-4:3 GL viewport
    // target), independent of the fixed 320x200 logical frameBuffer above.
    int GetWindowWidth(void) const { return width; }
    int GetWindowHeight(void) const { return height; }

    void ajusterContraste(float facteur);
    void ajusterLuminosite(float facteur);
    void appliquerTeinte(uint8_t r, uint8_t g, uint8_t b, float intensite);
    void redistributionCouleurs();
    void restaurerPalette();
    void interpolerPalettes(VGAPalette* palette1, VGAPalette* palette2, float facteur);
    
    float contrastFactor = 1.0f;
    float brightnessFactor = 1.0f;
    uint8_t tintR = 0;
    uint8_t tintG = 0;
    uint8_t tintB = 0;
    float tintIntensity = 0.0f;

};

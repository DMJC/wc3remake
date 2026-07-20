//
//  Screen->h
//  libRealSpace
//
//  Created by Fabien Sanglard on 1/27/2014.
//  Copyright (c) 2014 Fabien Sanglard. All rights reserved.
//

#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdlib.h>
#include <stdio.h>

#ifdef __cplusplus
}
#endif
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif
#ifndef __APPLE__
    #define GL_GLEXT_PROTOTYPES
    #include <GL/gl.h>
    #include <GL/glext.h>
#else
    #define GL_SILENCE_DEPRECATION
    #include <OpenGL/gl.h>
#endif
#include <SDL.h>
#include <memory>

#include "../commons/Matrix.h"

struct VRStereoEyeRenderInfo {
    int32_t width{0};
    int32_t height{0};
    Matrix projection;
    Matrix view;
};

class RSScreen {
private:
    inline static std::unique_ptr<RSScreen> s_instance{};
    GLuint m_postProcessTexture{0};
    GLuint m_shaderProgram{0};
    bool   m_postProcessReady{false};
public:
    RSScreen() = default;
    virtual ~RSScreen() = default;
    
    static RSScreen& getInstance() {
        if (!RSScreen::hasInstance()) {
            RSScreen::setInstance(std::make_unique<RSScreen>());
        }
        RSScreen& instance = RSScreen::instance();
        return instance;
    };
    static RSScreen& instance() {
        return *s_instance;
    }
    static void setInstance(std::unique_ptr<RSScreen> inst) {
        s_instance = std::move(inst);
    }

    // Optionnel : tester présence
    static bool hasInstance() { return (bool)s_instance; }

    
    virtual void init(int width, int height, bool fullscreen);
    virtual void openScreen(void);
    virtual void setTitle(const char* title);
    virtual void refresh(void);
    virtual void fxTurnOnTv();
    bool fx_cpc_palette{false};  // quantification palette CPC 6128
    bool fx_scanlines{false};    // scanlines CRT
    bool fx_fxaa{false};         // FXAA anti-aliasing
    float fx_pixel_scale{1.0f};   // pixelation scale (1.0 = normal, 2.0 = double size, etc.)
    virtual void initPostProcess();
    // --- VR stéréo (optionnel) ---
    // Par défaut: non supporté. Les implémentations VR (ex: VRScreen) surchargent.
    virtual uint32_t vrStereoEyeCount() const { return 0; }
    virtual bool vrPrepareStereoFrame(bool& outShouldRender) {
        outShouldRender = false;
        return false;
    }
    virtual bool vrBeginStereoEye(uint32_t /*eye*/,
                                  const Matrix& /*worldFromLocal*/,
                                  float /*metersToWorld*/,
                                  float /*zNear*/,
                                  float /*zFar*/,
                                  VRStereoEyeRenderInfo& /*out*/) {
        return false;
    }
    virtual void vrEndStereoFrame() {}

    SDL_Window* getSDLWindow() const { return m_window; }
    SDL_GLContext getGLContext() const { return m_glContext; }
    
    int32_t width;
    int32_t height;
    int32_t logical_width;
    int32_t logical_height;
    int32_t scale;
    bool is_spfx_finished{false};
    bool force_keyboard_capture{false};

    // The current 4:3-letterboxed viewport rect within the real drawable
    // area [0,width]x[0,height] — recomputed by openScreen() (called on
    // init and on every window resize). The original game's assets are all
    // authored for a 4:3 canvas (640x480 rooms, 320x200 movies); stretching
    // them to fill an arbitrary (e.g. 16:9) window distorts every sprite,
    // so every full-screen blit/viewport in the WC3 code paths should use
    // this rect instead of (0,0,width,height) directly, and every mouse
    // coordinate should go through windowToLogical() below rather than a
    // naive "mx * logicalW / width" (which implicitly assumes the whole
    // window is the logical canvas — wrong once there are letterbox bars).
    int32_t viewport_x{0};
    int32_t viewport_y{0};
    int32_t viewport_w{0};
    int32_t viewport_h{0};

    // Maps a mouse position (in the same window-coordinate space as
    // EventManager's mouse x/y) into a logicalW x logicalH virtual-screen
    // coordinate, accounting for the letterbox — positions in the black
    // bars clamp to the nearest in-bounds logical pixel rather than going
    // negative or off the far edge.
    void windowToLogical(int mx, int my, int logicalW, int logicalH, int& outX, int& outY) const;
    // Inverse of windowToLogical() — used to warp the real mouse cursor to
    // a specific logical-space point (e.g. cycleZoneFocus()'s Tab/right-
    // click zone-centering).
    void logicalToWindow(int lx, int ly, int logicalW, int logicalH, int& outX, int& outY) const;

protected:
    SDL_Window* m_window{nullptr};
    SDL_GLContext m_glContext{nullptr};
};

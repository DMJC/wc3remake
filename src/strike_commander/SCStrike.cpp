//
//  SCStrike.cpp
//  libRealSpace
//
//  Created by fabien sanglard on 1/28/2014.
//  Copyright (c) 2014 Fabien Sanglard. All rights reserved.
//

#include "precomp.h"
#include "../engine/GLBatch.h"
#include "../engine/gametimer.h"
#include <cctype>
#include <tuple>
#include <optional>
#include <algorithm>
#include <cmath>
#include <fstream>
#include "SCStrike.h"
#include "../realspace/block_def.h"
#define SC_WORLD 1100
#define RENDER_DISTANCE 40000.0f
// Half-length of VICTEST3.IFF (the TCS Victory's hangar-bay interior model,
// RSEntity::flight_deck_entity) along its long axis, per its exported OBJ
// vertex bounding box (Y spans roughly -1375..1348 in the ship's own local
// frame — see the flight_deck_entity render-mode comment in the actor loop
// below). Used as a simple "is the player still near/inside the bay"
// proximity radius.
#define HANGAR_INTERIOR_RADIUS 1500.0f
#define AUTOPILOTE_TIMEOUT 1000
#define AUTOPILOTE_SPEED 4

static Matrix invertRigidBodyMatrix(const Matrix& m) {
    Matrix out;
    out.Identity();
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            out.v[c][r] = m.v[r][c];
        }
    }
    const float tx = m.v[3][0];
    const float ty = m.v[3][1];
    const float tz = m.v[3][2];
    out.v[3][0] = -(out.v[0][0] * tx + out.v[1][0] * ty + out.v[2][0] * tz);
    out.v[3][1] = -(out.v[0][1] * tx + out.v[1][1] * ty + out.v[2][1] * tz);
    out.v[3][2] = -(out.v[0][2] * tx + out.v[1][2] * ty + out.v[2][2] * tz);
    return out;
}

static void drawTargetSquareOverlay(int32_t viewportW,
                                int32_t viewportH,
                                const Matrix& projection,
                                const Matrix& view,
                                const Vector3D& targetWorldPos) {
    Vector3DHomogeneous v = {targetWorldPos.x, targetWorldPos.y, targetWorldPos.z, 1.0f};
    Matrix viewM = view;
    Matrix projM = projection;
    Vector3DHomogeneous viewPos = viewM.multiplyMatrixVector(v);
    Vector3DHomogeneous clipPos = projM.multiplyMatrixVector(viewPos);

    if (clipPos.w <= 0.0f) {
        return;
    }

    const float ndcX = clipPos.x / clipPos.w;
    const float ndcY = clipPos.y / clipPos.w;
    const float ndcZ = clipPos.z / clipPos.w;

    if (ndcX < -1.0f || ndcX > 1.0f || ndcY < -1.0f || ndcY > 1.0f || ndcZ < -1.0f || ndcZ > 1.0f) {
        return;
    }

    const int px = (int)((ndcX * 0.5f + 0.5f) * (float)viewportW);
    const int py = (int)((0.5f - ndcY * 0.5f) * (float)viewportH);
    
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, (double)viewportW, (double)viewportH, 0.0, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);

    gb.color3f(0.0f, 1.0f, 0.0f);
    const float s = 8.0f;
    gb.begin(GL_LINE_LOOP);
    gb.vertex2f((float)px - s, (float)py - s);
    gb.vertex2f((float)px + s, (float)py - s);
    gb.vertex2f((float)px + s, (float)py + s);
    gb.vertex2f((float)px - s, (float)py + s);
    gb.end();

    glEnable(GL_DEPTH_TEST);
    

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);

}
void SCStrike::cleanupVirtualCockpitTextures() {
    static std::vector<Texture *> s_PrevFrameGLTex;
    static std::vector<Texture *> s_CurrentFrameGLTex;
    
    for (Texture* tex : s_PrevFrameGLTex) {
        delete tex;
    }
    s_PrevFrameGLTex.clear();
    s_PrevFrameGLTex.shrink_to_fit();
    
    for (Texture* tex : s_CurrentFrameGLTex) {
        delete tex;
    }
    s_CurrentFrameGLTex.clear();
    s_CurrentFrameGLTex.shrink_to_fit();
}
void SCStrike::renderVirtualCockpit() {
    if (this->cockpit->is_f16_cockpit) {
        renderVirtualF16Cockpit();
    } else {
        renderVirtualF22Cockpit();
    }
}
void SCStrike::renderVirtualF16Cockpit() {
    // this->cockpit->cockpit->REAL.OBJS (the SC1-style 3D virtual-cockpit
    // model) is legitimately null for every WC3 cockpit — WC3's own parse
    // path (InitFromWC3Ram/parseWC3_COCK) never populates it, since WC3
    // renders its cockpit via the 2D VDU/instrument sprite system instead.
    // Was previously dereferenced unconditionally below (crash: RealObjs::
    // OBJS had no default initializer either, so this read garbage instead
    // of a clean null — see that field's own comment).
    if (this->cockpit->cockpit == nullptr || this->cockpit->cockpit->REAL.OBJS == nullptr) {
        return;
    }
    static std::vector<Texture *> s_PrevFrameGLTex;
    static std::vector<Texture *> s_CurrentFrameGLTex;

    if (!s_PrevFrameGLTex.empty()) {
        for (Texture* tex : s_PrevFrameGLTex) {
            delete tex;
        }
        s_PrevFrameGLTex.clear();
        s_PrevFrameGLTex.shrink_to_fit();
    }
    s_PrevFrameGLTex.swap(s_CurrentFrameGLTex); 
    const Vector3D camPos = {
        this->camera->getPosition().x,
        this->camera->getPosition().y,
        this->camera->getPosition().z
    };
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glTranslatef(camPos.x, camPos.y, camPos.z);

    Vector3D cockpit_pos = {0.0f, 0.0f, 0.0f};

    

    Vector3D cockpit_rot;
    cockpit_rot = {(this->player_plane->azimuthf+900)/10.0f,
                    this->player_plane->elevationf/10.0f,
                    -this->player_plane->twist/10.0f};

    Vector3D cockpit_ajustement = { 0.0f,-(float) this->eye_y,0.0f};
    if (this->camera->isUsingCustomMatrices()) {
        const float cockpitScale = 1.0f; // ajuste: 0.7 .. 0.95
        //cockpit_ajustement = { -0.225f,-(float) this->eye_y+2.35f,0.0f};
        cockpit_ajustement = { -0.55f,-(float) this->eye_y,0.0f};
        Renderer.drawModel(this->cockpit->cockpit->REAL.OBJS, Renderer.lodLevel, cockpit_pos, cockpit_rot, cockpit_ajustement, cockpitScale);
    } else {
        Renderer.drawModel(this->cockpit->cockpit->REAL.OBJS, Renderer.lodLevel, cockpit_pos, cockpit_rot, cockpit_ajustement);
    }

    
    this->cockpit->cannonAngularOffset = gunsight_hud_offset;
    CockpitFace prev_face = this->cockpit->face;
    this->cockpit->face = CockpitFace::CP_BIG;
    this->cockpit->RenderHUD();
    this->cockpit->face = prev_face;
    if (this->cockpit->hud != nullptr) {
        Texture *hud_texture = new Texture();
        hud_texture->animated = true;
        RSImage *hud_image = new RSImage();
        hud_image->palette = &this->cockpit->palette;
        hud_image->data = this->cockpit->hud_framebuffer->framebuffer;
        hud_image->width = this->cockpit->hud_framebuffer->width;
        hud_image->height = this->cockpit->hud_framebuffer->height;
        hud_texture->set(hud_image);
        hud_texture->updateContent(hud_image);
        Renderer.drawTexturedQuad(
            cockpit_pos,
            cockpit_rot,
            {
                {5.8f, 2.0f, -1.22f}, 
                {5.8f, 2.0f, 1.35f},
                {6.0f, -0.8f, 1.35f},
                {6.0f, -0.8f, -1.22f}
            },
            hud_texture
        );
        hud_image->data = nullptr;
        delete hud_image;
        s_CurrentFrameGLTex.push_back(hud_texture);
    }
    if (this->cockpit->cockpit->MONI.SHAP.data != nullptr) {        
        Texture *mfd_right_texture = new Texture();
        mfd_right_texture->animated = true;
        RSImage *mfd_right_image = new RSImage();
        mfd_right_image->palette = &this->cockpit->palette;
        if (this->cockpit->show_cam) {
            cockpit->RenderMFDSCamera({0,0}, cockpit->mfd_right_framebuffer);
        } else if (this->cockpit->show_damage) {
            cockpit->RenderMFDSDamage({0,0}, cockpit->mfd_right_framebuffer);
        }else {
            cockpit->RenderMFDSWeapon({0,0}, cockpit->mfd_right_framebuffer);
        }
        
        mfd_right_image->data = this->cockpit->mfd_right_framebuffer->framebuffer;
        mfd_right_image->width = this->cockpit->mfd_right_framebuffer->width;
        mfd_right_image->height = this->cockpit->mfd_right_framebuffer->height;
        mfd_right_texture->set(mfd_right_image);
        mfd_right_texture->updateContent(mfd_right_image);
        Renderer.drawTexturedQuad(
            cockpit_pos,
            cockpit_rot,
            {
                {6.2f, -2.5f, -3.0f}, 
                {6.2f, -2.5f, -1.15f},
                {6.1f, -4.4f, -1.15f},
                {6.1f, -4.4f, -3.0f}
            },
            mfd_right_texture
        );
        mfd_right_image->data = nullptr;
        delete mfd_right_image;
        s_CurrentFrameGLTex.push_back(mfd_right_texture);
        if (this->cockpit->show_comm) {
            cockpit->mfd_left_framebuffer->fillWithColor(0);
            cockpit->RenderMFDSComm({0,0}, cockpit->comm_target, cockpit->mfd_left_framebuffer);
        } else {
            cockpit->RenderMFDSRadar({0,0}, cockpit->radar_zoom*20000.0f, this->cockpit->radar_mode, cockpit->mfd_left_framebuffer);
        }
        Texture *mfd_left_texture = new Texture();
        mfd_left_texture->animated = true;
        RSImage *mfd_left_image = new RSImage();
        mfd_left_image->palette = &this->cockpit->palette;
        mfd_left_image->data = this->cockpit->mfd_left_framebuffer->framebuffer;
        mfd_left_image->width = this->cockpit->mfd_left_framebuffer->width;
        mfd_left_image->height = this->cockpit->mfd_left_framebuffer->height;
        mfd_left_texture->set(mfd_left_image);
        mfd_left_texture->updateContent(mfd_left_image);
        Renderer.drawTexturedQuad(
            cockpit_pos,
            cockpit_rot,
            {
                {6.2f, -2.5f, 1.15f}, 
                {6.2f, -2.5f, 3.0f},
                {6.1f, -4.4f, 3.0f},
                {6.1f, -4.4f, 1.15f}
            },
            mfd_left_texture
        );
        mfd_left_image->data = nullptr;
        delete mfd_left_image;
        s_CurrentFrameGLTex.push_back(mfd_left_texture);
    }
    if (this->cockpit->cockpit->MONI.INST.RAWS.NORM.data != nullptr) {
        cockpit->raws_framebuffer->fillWithColor(0);
        cockpit->RenderRAWSBig({0,0}, cockpit->raws_framebuffer);
        Texture *raws_texture = new Texture();
        raws_texture->animated = true;
        RSImage *raws_image = new RSImage();
        raws_image->palette = &this->cockpit->palette;
        raws_image->data = this->cockpit->raws_framebuffer->framebuffer;
        raws_image->width = this->cockpit->raws_framebuffer->width;
        raws_image->height = this->cockpit->raws_framebuffer->height;
        raws_texture->set(raws_image);
        raws_texture->updateContent(raws_image);
        Renderer.drawTexturedQuad(
            cockpit_pos,
            cockpit_rot,
            {
                {6.2f, -1.38f, -2.10f}, 
                {6.2f, -1.38f, -1.15f},
                {6.1f, -2.45f, -1.15f},
                {6.1f, -2.45f, -2.10f}
            },
            raws_texture
        );
        raws_image->data = nullptr;
        delete raws_image;
        s_CurrentFrameGLTex.push_back(raws_texture);
    }
    if (cockpit->target_framebuffer != nullptr && !camera->isUsingCustomMatrices()) {
    
        cockpit->target_framebuffer->fillWithColor(255);
        cockpit->RenderTargetWithCam({0,0}, cockpit->target_framebuffer);
        Texture *target_texture = new Texture();
        target_texture->animated = true;
        RSImage *target_image = new RSImage();
        target_image->palette = &this->cockpit->palette;
        target_image->data = this->cockpit->target_framebuffer->framebuffer;
        target_image->width = this->cockpit->target_framebuffer->width;
        target_image->height = this->cockpit->target_framebuffer->height;
        target_texture->set(target_image);
        target_texture->updateContent(target_image);
        // Calculate a position in front of the camera
        // Create a fullscreen quad in front of the camera
        // Adjust for the camera Y-axis offset
        Vector3D quadPos = camera->getPosition() + camera->getForward() * 8.0f;
        
        // Set up vectors for billboard creation
        Vector3D forward = camera->getForward();
        Vector3D right = camera->getRight();
        Vector3D up = camera->getUp();

        
        
        // Scale factors for quad size
        float width =4.92f;
        float height = 3.22f;
        // Compensate for the 0.45f Y-axis projection offset
        // We need to shift the entire quad upwards by adding an offset in the up direction
        float projectionYOffset = -1.5f;
        Vector3D offsetCompensation = up * projectionYOffset;
        
        // Apply the offset to the quad center position
        Vector3D adjustedQuadPos = quadPos + offsetCompensation;
        
        // Define the quad's corners in world space with the adjusted center position
        Vector3D topLeft = adjustedQuadPos + (up * height) - (right * width);
        Vector3D topRight = adjustedQuadPos + (up * height) + (right * width);
        Vector3D bottomRight = adjustedQuadPos - (up * height) + (right * width);
        Vector3D bottomLeft = adjustedQuadPos - (up * height) - (right * width);
        
        // Draw the quad with the target texture
        Renderer.drawTexturedQuad(
            {0, 0, 0}, // No position offset needed as we're using world coordinates
            {0, 0, 0}, // No rotation needed as we're manually setting vertices
            {
                topLeft,
                topRight,
                bottomRight,
                bottomLeft
            },
            target_texture
        );

        target_image->data = nullptr;
        delete target_image;
        s_CurrentFrameGLTex.push_back(target_texture);
    }
    if (this->cockpit->alti_framebuffer != nullptr && this->cockpit->speed_framebuffer != nullptr) {
        cockpit->alti_framebuffer->fillWithColor(0);
        cockpit->RenderAlti({0,0}, cockpit->alti_framebuffer);
        Texture *alti_texture = new Texture();
        alti_texture->animated = true;
        RSImage *alti_image = new RSImage();
        alti_image->palette = &this->cockpit->palette;
        alti_image->data = this->cockpit->alti_framebuffer->framebuffer;
        alti_image->width = this->cockpit->alti_framebuffer->width;
        alti_image->height = this->cockpit->alti_framebuffer->height;
        alti_texture->set(alti_image);
        alti_texture->updateContent(alti_image);
        Renderer.drawTexturedQuad(
            cockpit_pos,
            cockpit_rot,
            {
                {6.1f, -3.15f, 0.020f}, 
                {6.1f, -3.15f, 0.90f},
                {5.7f, -3.92f, 0.90f},
                {5.7f, -3.92f, 0.020f}
            },
            alti_texture
        );

        alti_image->data = nullptr;    
        delete alti_image;
        s_CurrentFrameGLTex.push_back(alti_texture);


        cockpit->speed_framebuffer->fillWithColor(0);
        cockpit->RenderSpeedOmetter({0,0}, cockpit->speed_framebuffer);
        Texture *speed_texture = new Texture();
        speed_texture->animated = true;
        RSImage *speed_image = new RSImage();
        speed_image->palette = &this->cockpit->palette;
        speed_image->data = this->cockpit->speed_framebuffer->framebuffer;
        speed_image->width = this->cockpit->speed_framebuffer->width;
        speed_image->height = this->cockpit->speed_framebuffer->height;
        speed_texture->set(speed_image);
        speed_texture->updateContent(speed_image);
        Renderer.drawTexturedQuad(
            cockpit_pos,
            cockpit_rot,
            {
                {6.1f, -3.15f, -0.90f}, 
                {6.1f, -3.15f, -0.020f},
                {5.7f, -3.92f, -0.020f},
                {5.7f, -3.92f, -0.90f}
            },
            speed_texture
        );

        speed_image->data = nullptr;
        delete speed_image;
        s_CurrentFrameGLTex.push_back(speed_texture);
        if (this->cockpit->shield_framebuffer != nullptr) {
            cockpit->shield_framebuffer->fillWithColor(0);
            cockpit->RenderShieldGauge({0,0}, cockpit->shield_framebuffer);
            Texture *shield_texture = new Texture();
            shield_texture->animated = true;
            RSImage *shield_image = new RSImage();
            shield_image->palette = &this->cockpit->palette;
            shield_image->data = this->cockpit->shield_framebuffer->framebuffer;
            shield_image->width = this->cockpit->shield_framebuffer->width;
            shield_image->height = this->cockpit->shield_framebuffer->height;
            shield_texture->set(shield_image);
            shield_texture->updateContent(shield_image);
            // Placed just below the altimeter/speed cluster — a first-guess
            // position, not derived from any parsed layout metadata (WC3's
            // shape-pak carries no per-id screen coordinates, see
            // RSCockpit::instrumentShapes), same as how alti/speed/RAWS's
            // own quad corners above were arrived at. Needs live-visual
            // tuning once in-game.
            Renderer.drawTexturedQuad(
                cockpit_pos,
                cockpit_rot,
                {
                    {6.1f, -3.15f, 1.10f},
                    {6.1f, -3.15f, 1.60f},
                    {5.7f, -3.92f, 1.60f},
                    {5.7f, -3.92f, 1.10f}
                },
                shield_texture
            );
            shield_image->data = nullptr;
            delete shield_image;
            s_CurrentFrameGLTex.push_back(shield_texture);
        }
        cockpit->comm_framebuffer->fillWithColor(0);

        if (cockpit->RenderCommMessages({0,13}, cockpit->comm_framebuffer)) {
            Texture *comm_texture = new Texture();
            comm_texture->animated = true;
            RSImage *comm_image = new RSImage();
            comm_image->palette = &this->cockpit->palette;
            comm_image->data = this->cockpit->comm_framebuffer->framebuffer;
            comm_image->width = this->cockpit->comm_framebuffer->width;
            comm_image->height = this->cockpit->comm_framebuffer->height;
            comm_texture->set(comm_image);
            comm_texture->updateContent(comm_image);
            Renderer.drawTexturedQuad(
                cockpit_pos,
                cockpit_rot,
                {
                    {6.1f, -0.95f, -1.90f}, 
                    {6.1f, -0.95f, 2.020f},
                    {6.1f, -1.10f, 2.020f},
                    {6.1f, -1.10f, -1.90f}
                },
                comm_texture
            );
            comm_image->data = nullptr;        
            delete comm_image;
            s_CurrentFrameGLTex.push_back(comm_texture);   
        }
        if (this->mouse_control) {
            this->virtual_mouse_cockpit_buffer->fillWithColor(0);
            this->virtual_mouse_cockpit_buffer->line(160,0,160,200, 223);
            this->virtual_mouse_cockpit_buffer->line(0,100,320,100, 223);
            this->virtual_mouse_cockpit_buffer->rect_slow(1,1,319,199, 223);
            Point2D cursorPos = Mouse.position;
            cursorPos.x -= 4;
            cursorPos.y -= 4;
            Mouse.appearances[SCMouse::CURSOR]->SetPosition(&cursorPos);
            this->virtual_mouse_cockpit_buffer->drawShape(Mouse.appearances[SCMouse::CURSOR]);
            Texture *mouse_texture = new Texture();
            mouse_texture->animated = true;
            RSImage *mouse_image = new RSImage();
            mouse_image->palette = &this->cockpit->palette;
            mouse_image->data = this->virtual_mouse_cockpit_buffer->framebuffer;
            mouse_image->width = this->virtual_mouse_cockpit_buffer->width;
            mouse_image->height = this->virtual_mouse_cockpit_buffer->height;
            mouse_texture->set(mouse_image);
            mouse_texture->updateContent(mouse_image);
            Renderer.drawTexturedQuad(
                cockpit_pos,
                cockpit_rot,
                {
                    {6.05f, -1.10f, -1.0f}, 
                    {6.05f, -1.10f, 1.0f},
                    {6.05f, -2.10f, 1.0f},
                    {6.05f, -2.10f, -1.0f}
                },
                mouse_texture
            );
            mouse_image->data = nullptr;        
            delete mouse_image;
            s_CurrentFrameGLTex.push_back(mouse_texture);
        }
        
    }
    glPopMatrix();
}
void SCStrike::renderVirtualF22Cockpit() {
    // See renderVirtualF16Cockpit's own comment — same null REAL.OBJS
    // crash risk, same fix.
    if (this->cockpit->cockpit == nullptr || this->cockpit->cockpit->REAL.OBJS == nullptr) {
        return;
    }
    static std::vector<Texture *> s_PrevFrameGLTex;
    static std::vector<Texture *> s_CurrentFrameGLTex;

    if (!s_PrevFrameGLTex.empty()) {
        for (Texture* tex : s_PrevFrameGLTex) {
            delete tex;
        }
        s_PrevFrameGLTex.clear();
        s_PrevFrameGLTex.shrink_to_fit();
    }
    s_PrevFrameGLTex.swap(s_CurrentFrameGLTex); 
    const Vector3D camPos = {
        this->camera->getPosition().x,
        this->camera->getPosition().y,
        this->camera->getPosition().z
    };
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glTranslatef(camPos.x, camPos.y, camPos.z);

    Vector3D cockpit_pos = {0.0f, 0.0f, 0.0f};

    

    Vector3D cockpit_rot;
    cockpit_rot = {(this->player_plane->azimuthf+900)/10.0f,
                    this->player_plane->elevationf/10.0f,
                    -this->player_plane->twist/10.0f};

    Vector3D cockpit_ajustement = { 0.0f,-(float) this->eye_y,0.0f};
    if (this->camera->isUsingCustomMatrices()) {
        const float cockpitScale = 1.0f; // ajuste: 0.7 .. 0.95
        //cockpit_ajustement = { -0.225f,-(float) this->eye_y+2.35f,0.0f};
        cockpit_ajustement = { -0.55f,-(float) this->eye_y,0.0f};
        Renderer.drawModel(this->cockpit->cockpit->REAL.OBJS, Renderer.lodLevel, cockpit_pos, cockpit_rot, cockpit_ajustement, cockpitScale);
    } else {
        Renderer.drawModel(this->cockpit->cockpit->REAL.OBJS, Renderer.lodLevel, cockpit_pos, cockpit_rot, cockpit_ajustement);
    }

    
    this->cockpit->cannonAngularOffset = gunsight_hud_offset;
    CockpitFace prev_face = this->cockpit->face;
    this->cockpit->face = CockpitFace::CP_BIG;
    this->cockpit->RenderHUD();
    this->cockpit->face = prev_face;
    if (this->cockpit->hud != nullptr) {
        Texture *hud_texture = new Texture();
        hud_texture->animated = true;
        RSImage *hud_image = new RSImage();
        hud_image->palette = &this->cockpit->palette;
        hud_image->data = this->cockpit->hud_framebuffer->framebuffer;
        hud_image->width = this->cockpit->hud_framebuffer->width;
        hud_image->height = this->cockpit->hud_framebuffer->height;
        hud_texture->set(hud_image);
        hud_texture->updateContent(hud_image);
        Renderer.drawTexturedQuad(
            cockpit_pos,
            cockpit_rot,
            {
                {5.8f, 2.0f, -1.22f}, 
                {5.8f, 2.0f, 1.35f},
                {6.0f, -0.8f, 1.35f},
                {6.0f, -0.8f, -1.22f}
            },
            hud_texture
        );
        hud_image->data = nullptr;
        delete hud_image;
        s_CurrentFrameGLTex.push_back(hud_texture);
    }
    if (this->cockpit->cockpit->MONI.SHAP.data != nullptr) {        
        Texture *mfd_right_texture = new Texture();
        mfd_right_texture->animated = true;
        RSImage *mfd_right_image = new RSImage();
        mfd_right_image->palette = &this->cockpit->palette;
        if (this->cockpit->show_cam) {
            cockpit->RenderMFDSCamera({0,0}, cockpit->mfd_right_framebuffer);
        } else if (this->cockpit->show_damage) {
            cockpit->RenderMFDSDamage({0,0}, cockpit->mfd_right_framebuffer);
        }else {
            cockpit->RenderMFDSWeapon({0,0}, cockpit->mfd_right_framebuffer);
        }
        
        mfd_right_image->data = this->cockpit->mfd_right_framebuffer->framebuffer;
        mfd_right_image->width = this->cockpit->mfd_right_framebuffer->width;
        mfd_right_image->height = this->cockpit->mfd_right_framebuffer->height;
        mfd_right_texture->set(mfd_right_image);
        mfd_right_texture->updateContent(mfd_right_image);
        Renderer.drawTexturedQuad(
            cockpit_pos,
            cockpit_rot,
            {
                {8.2f, -2.95f, -3.3f}, 
                {8.2f, -2.95f, -1.35f},
                {8.1f, -5.0f, -1.35f},
                {8.1f, -5.0f, -3.3f}
            },
            mfd_right_texture
        );
        mfd_right_image->data = nullptr;
        delete mfd_right_image;
        s_CurrentFrameGLTex.push_back(mfd_right_texture);
        if (this->cockpit->show_comm) {
            cockpit->mfd_left_framebuffer->fillWithColor(0);
            cockpit->RenderMFDSComm({0,0}, cockpit->comm_target, cockpit->mfd_left_framebuffer);
        } else {
            cockpit->RenderMFDSRadar({0,0}, cockpit->radar_zoom*20000.0f, this->cockpit->radar_mode, cockpit->mfd_left_framebuffer);
        }
        Texture *mfd_left_texture = new Texture();
        mfd_left_texture->animated = true;
        RSImage *mfd_left_image = new RSImage();
        mfd_left_image->palette = &this->cockpit->palette;
        mfd_left_image->data = this->cockpit->mfd_left_framebuffer->framebuffer;
        mfd_left_image->width = this->cockpit->mfd_left_framebuffer->width;
        mfd_left_image->height = this->cockpit->mfd_left_framebuffer->height;
        mfd_left_texture->set(mfd_left_image);
        mfd_left_texture->updateContent(mfd_left_image);
        Renderer.drawTexturedQuad(
            cockpit_pos,
            cockpit_rot,
            {
                {8.2f, -2.95f, 1.35f}, 
                {8.2f, -2.95f, 3.3f},
                {8.1f, -5.0f, 3.3f},
                {8.1f, -5.0f, 1.35f}
            },
            mfd_left_texture
        );
        mfd_left_image->data = nullptr;
        delete mfd_left_image;
        s_CurrentFrameGLTex.push_back(mfd_left_texture);
    }
    if (this->cockpit->cockpit->MONI.INST.RAWS.NORM.data != nullptr) {
        cockpit->raws_framebuffer->fillWithColor(0);
        cockpit->RenderRAWSBig({0,0}, cockpit->raws_framebuffer);
        Texture *raws_texture = new Texture();
        raws_texture->animated = true;
        RSImage *raws_image = new RSImage();
        raws_image->palette = &this->cockpit->palette;
        raws_image->data = this->cockpit->raws_framebuffer->framebuffer;
        raws_image->width = this->cockpit->raws_framebuffer->width;
        raws_image->height = this->cockpit->raws_framebuffer->height;
        raws_texture->set(raws_image);
        raws_texture->updateContent(raws_image);
        Renderer.drawTexturedQuad(
            cockpit_pos,
            cockpit_rot,
            {
                {5.8f, -0.95f, -0.54f}, 
                {5.8f, -0.95f, 0.64f},
                {5.8f, -1.835f, 0.64f},
                {5.8f, -1.835f, -0.54f}
            },
            raws_texture
        );
        raws_image->data = nullptr;
        delete raws_image;
        s_CurrentFrameGLTex.push_back(raws_texture);
    }
    if (cockpit->target_framebuffer != nullptr && !camera->isUsingCustomMatrices()) {
    
        cockpit->target_framebuffer->fillWithColor(255);
        cockpit->RenderTargetWithCam({0,0}, cockpit->target_framebuffer);
        Texture *target_texture = new Texture();
        target_texture->animated = true;
        RSImage *target_image = new RSImage();
        target_image->palette = &this->cockpit->palette;
        target_image->data = this->cockpit->target_framebuffer->framebuffer;
        target_image->width = this->cockpit->target_framebuffer->width;
        target_image->height = this->cockpit->target_framebuffer->height;
        target_texture->set(target_image);
        target_texture->updateContent(target_image);
        // Calculate a position in front of the camera
        // Create a fullscreen quad in front of the camera
        // Adjust for the camera Y-axis offset
        Vector3D quadPos = camera->getPosition() + camera->getForward() * 8.0f;
        
        // Set up vectors for billboard creation
        Vector3D forward = camera->getForward();
        Vector3D right = camera->getRight();
        Vector3D up = camera->getUp();

        
        
        // Scale factors for quad size
        float width =4.92f;
        float height = 3.22f;
        // Compensate for the 0.45f Y-axis projection offset
        // We need to shift the entire quad upwards by adding an offset in the up direction
        float projectionYOffset = -1.5f;
        Vector3D offsetCompensation = up * projectionYOffset;
        
        // Apply the offset to the quad center position
        Vector3D adjustedQuadPos = quadPos + offsetCompensation;
        
        // Define the quad's corners in world space with the adjusted center position
        Vector3D topLeft = adjustedQuadPos + (up * height) - (right * width);
        Vector3D topRight = adjustedQuadPos + (up * height) + (right * width);
        Vector3D bottomRight = adjustedQuadPos - (up * height) + (right * width);
        Vector3D bottomLeft = adjustedQuadPos - (up * height) - (right * width);
        
        // Draw the quad with the target texture
        Renderer.drawTexturedQuad(
            {0, 0, 0}, // No position offset needed as we're using world coordinates
            {0, 0, 0}, // No rotation needed as we're manually setting vertices
            {
                topLeft,
                topRight,
                bottomRight,
                bottomLeft
            },
            target_texture
        );

        target_image->data = nullptr;
        delete target_image;
        s_CurrentFrameGLTex.push_back(target_texture);
    }
        
    if (cockpit->RenderCommMessages({0,13}, cockpit->comm_framebuffer)) {
        cockpit->comm_framebuffer->fillWithColor(0);

        Texture *comm_texture = new Texture();
        comm_texture->animated = true;
        RSImage *comm_image = new RSImage();
        comm_image->palette = &this->cockpit->palette;
        comm_image->data = this->cockpit->comm_framebuffer->framebuffer;
        comm_image->width = this->cockpit->comm_framebuffer->width;
        comm_image->height = this->cockpit->comm_framebuffer->height;
        comm_texture->set(comm_image);
        comm_texture->updateContent(comm_image);
        Renderer.drawTexturedQuad(
            cockpit_pos,
            cockpit_rot,
            {
                {6.1f, -0.95f, -1.90f}, 
                {6.1f, -0.95f, 2.020f},
                {6.1f, -1.10f, 2.020f},
                {6.1f, -1.10f, -1.90f}
            },
            comm_texture
        );
        comm_image->data = nullptr;        
        delete comm_image;
        s_CurrentFrameGLTex.push_back(comm_texture);   
    }
    if (this->mouse_control) {
        this->virtual_mouse_cockpit_buffer->fillWithColor(0);
        this->virtual_mouse_cockpit_buffer->line(160,0,160,200, 223);
        this->virtual_mouse_cockpit_buffer->line(0,100,320,100, 223);
        this->virtual_mouse_cockpit_buffer->rect_slow(1,1,319,199, 223);
        Point2D cursorPos = Mouse.position;
        cursorPos.x -= 4;
        cursorPos.y -= 4;
        Mouse.appearances[SCMouse::CURSOR]->SetPosition(&cursorPos);
        this->virtual_mouse_cockpit_buffer->drawShape(Mouse.appearances[SCMouse::CURSOR]);
        Texture *mouse_texture = new Texture();
        mouse_texture->animated = true;
        RSImage *mouse_image = new RSImage();
        mouse_image->palette = &this->cockpit->palette;
        mouse_image->data = this->virtual_mouse_cockpit_buffer->framebuffer;
        mouse_image->width = this->virtual_mouse_cockpit_buffer->width;
        mouse_image->height = this->virtual_mouse_cockpit_buffer->height;
        mouse_texture->set(mouse_image);
        mouse_texture->updateContent(mouse_image);
        Renderer.drawTexturedQuad(
            cockpit_pos,
            cockpit_rot,
            {
                {6.05f, -1.10f, -1.0f}, 
                {6.05f, -1.10f, 1.0f},
                {6.05f, -2.10f, 1.0f},
                {6.05f, -2.10f, -1.0f}
            },
            mouse_texture
        );
        mouse_image->data = nullptr;        
        delete mouse_image;
        s_CurrentFrameGLTex.push_back(mouse_texture);
    }
    glPopMatrix();
}
/**
 * @brief Constructor
 *
 * Initializes the SCStrike object with a default camera position and
 * other variables.
 */
SCStrike::SCStrike() {
    this->camera_mode = 0;
    this->camera_pos = {-169.0f, 79.0f, -189.0f};
    this->counter = 0;
    if (Game == nullptr) {
        Game = &GameEngine::instance();
    }
    this->m_keyboard = Game->getKeyboard();
    if (this->Screen == nullptr) {
        this->Screen = &RSScreen::instance();
    }
}

SCStrike::~SCStrike() {
    Game->direct_mouse_control = false;
}

namespace {
// Afterburner sound source, user-identified: 48_8bit_11025.wav (8-bit PCM
// mono, 11025Hz, ~3.74s) — not part of any TRE archive, loaded as a loose
// file the same way RSMixer already loads STRIKE.wopl ("./assets/...",
// copied from resources/ at build time), since AssetManager has no entry
// for it.
struct WavInfo {
    bool valid{false};
    uint32_t sampleRate{0};
    uint16_t channels{0};
    uint16_t bitsPerSample{0};
    size_t dataOffset{0};
    size_t dataSize{0};
};

uint32_t ReadU32LE(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint16_t ReadU16LE(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

// Walks a RIFF/WAVE buffer's chunks to find "fmt " and "data" — not
// assuming the canonical 44-byte fixed layout, since some WAV writers
// insert extra chunks (LIST/fact/etc.) before "data".
WavInfo ParseWav(const std::vector<uint8_t> &buf) {
    WavInfo info;
    if (buf.size() < 12 || memcmp(buf.data(), "RIFF", 4) != 0 || memcmp(buf.data() + 8, "WAVE", 4) != 0) {
        return info;
    }
    size_t pos = 12;
    while (pos + 8 <= buf.size()) {
        char id[5] = {0};
        memcpy(id, buf.data() + pos, 4);
        uint32_t chunkSize = ReadU32LE(buf.data() + pos + 4);
        size_t bodyStart = pos + 8;
        if (bodyStart + chunkSize > buf.size()) break;
        if (memcmp(id, "fmt ", 4) == 0 && chunkSize >= 16) {
            const uint8_t *p = buf.data() + bodyStart;
            info.channels = ReadU16LE(p + 2);
            info.sampleRate = ReadU32LE(p + 4);
            info.bitsPerSample = ReadU16LE(p + 14);
        } else if (memcmp(id, "data", 4) == 0) {
            info.dataOffset = bodyStart;
            info.dataSize = chunkSize;
        }
        pos = bodyStart + chunkSize + (chunkSize & 1); // chunks are word-aligned
    }
    info.valid = info.sampleRate > 0 && info.channels > 0 && info.bitsPerSample > 0 && info.dataSize > 0;
    return info;
}

// Builds a standalone, canonical-header WAV containing only the last
// `tailSeconds` of `full`'s audio, so it can be looped seamlessly on its
// own once the afterburner key has been held past the full clip's length.
std::vector<uint8_t> BuildWavTail(const std::vector<uint8_t> &full, const WavInfo &info, float tailSeconds) {
    size_t frameBytes = (size_t)info.channels * (info.bitsPerSample / 8);
    size_t tailBytes = (size_t)(tailSeconds * (float)info.sampleRate) * frameBytes;
    if (tailBytes == 0 || tailBytes > info.dataSize) tailBytes = info.dataSize;
    tailBytes -= tailBytes % frameBytes; // keep frame-aligned
    size_t tailStart = info.dataOffset + info.dataSize - tailBytes;

    std::vector<uint8_t> out(44 + tailBytes);
    uint8_t *h = out.data();
    memcpy(h, "RIFF", 4);
    uint32_t riffSize = (uint32_t)(36 + tailBytes);
    memcpy(h + 4, &riffSize, 4);
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);
    uint32_t fmtSize = 16;
    memcpy(h + 16, &fmtSize, 4);
    uint16_t audioFormat = 1; // PCM
    memcpy(h + 20, &audioFormat, 2);
    memcpy(h + 22, &info.channels, 2);
    memcpy(h + 24, &info.sampleRate, 4);
    uint32_t byteRate = info.sampleRate * frameBytes;
    memcpy(h + 28, &byteRate, 4);
    uint16_t blockAlign = (uint16_t)frameBytes;
    memcpy(h + 32, &blockAlign, 2);
    memcpy(h + 34, &info.bitsPerSample, 2);
    memcpy(h + 36, "data", 4);
    uint32_t dataSize32 = (uint32_t)tailBytes;
    memcpy(h + 40, &dataSize32, 4);
    memcpy(h + 44, full.data() + tailStart, tailBytes);
    return out;
}

bool g_afterburnerSoundLoaded = false;
std::vector<uint8_t> g_afterburnerFullWav;
std::vector<uint8_t> g_afterburnerTailWav;
float g_afterburnerFullDuration = 0.0f;

void LoadAfterburnerSoundOnce() {
    if (g_afterburnerSoundLoaded) return;
    g_afterburnerSoundLoaded = true;
    std::ifstream f("./assets/48_8bit_11025.wav", std::ios::binary);
    if (!f) {
        printf("Afterburner sound not found at ./assets/48_8bit_11025.wav\n");
        return;
    }
    g_afterburnerFullWav.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    WavInfo info = ParseWav(g_afterburnerFullWav);
    if (!info.valid) {
        printf("Afterburner sound: failed to parse WAV header\n");
        g_afterburnerFullWav.clear();
        return;
    }
    size_t frameBytes = (size_t)info.channels * (info.bitsPerSample / 8);
    g_afterburnerFullDuration = (float)info.dataSize / (float)(info.sampleRate * frameBytes);
    g_afterburnerTailWav = BuildWavTail(g_afterburnerFullWav, info, 1.0f);
}
} // namespace

// Real behavior (user-confirmed): the afterburner sound plays through
// once from the moment the key is first pressed; once only the last
// second of the clip remains, that last second loops for as long as the
// key stays held (rather than restarting the whole clip or cutting off
// mid-sound); releasing the key stops it immediately. There's no
// equivalent sample anywhere in a mission's own sound bank
// (current_mission->sound.sounds), so this loads/caches the loose WAV
// once instead of going through AssetManager.
void SCStrike::updateAfterburnerSound(bool engaged) {
    LoadAfterburnerSoundOnce();
    if (g_afterburnerFullWav.empty()) return;
    static constexpr int kAfterburnerChannel = 6;

    if (engaged && !this->afterburner_sound_engaged_prev) {
        Mixer.playSoundVoc(g_afterburnerFullWav.data(), g_afterburnerFullWav.size(), kAfterburnerChannel, 0);
        this->afterburner_sound_elapsed = 0.0f;
        this->afterburner_sound_tail_started = false;
    } else if (engaged) {
        this->afterburner_sound_elapsed += GameTimer::getInstance().getDeltaTime();
        if (!this->afterburner_sound_tail_started &&
            this->afterburner_sound_elapsed >= g_afterburnerFullDuration - 1.0f) {
            Mixer.playSoundVoc(g_afterburnerTailWav.data(), g_afterburnerTailWav.size(), kAfterburnerChannel, -1);
            this->afterburner_sound_tail_started = true;
        }
    } else if (this->afterburner_sound_engaged_prev) {
        Mixer.stopSound(kAfterburnerChannel);
        this->afterburner_sound_tail_started = false;
    }
    this->afterburner_sound_engaged_prev = engaged;
}

void SCStrike::registerSimulatorInputs() {
    
}
void SCStrike::autopilotCompute() {
    // No waypoint queued yet (see SCCockpit::Update()'s own guard for why
    // that's a normal, reachable state) — nothing to autopilot toward.
    if (this->current_mission->waypoints.empty() ||
        (size_t)this->nav_point_id >= this->current_mission->waypoints.size()) {
        return;
    }
    Vector2D destination = {this->current_mission->waypoints[this->nav_point_id]->spot->position.x,
                            this->current_mission->waypoints[this->nav_point_id]->spot->position.z};
    this->autopilot_target_azimuth = atan2(this->player_plane->x-destination.x, this->player_plane->z-destination.y);
    this->autopilot_target_azimuth = radToDegree(this->autopilot_target_azimuth);
    this->player_plane->yaw = 0.0f;
    this->player_plane->pitch = 0.0f;
    this->player_plane->roll = 0.0f;
    float dest_y = this->current_mission->waypoints[this->nav_point_id]->spot->position.y;
    Vector2D origine = {this->player_plane->x, this->player_plane->z};
    std::vector<Vector2D> path;
    for (auto area: this->current_mission->mission->mission_data.areas) {
        {
            bool area_active = false;
            for (auto scen: this->current_mission->mission->mission_data.scenes) {
                if (scen->area_id == area->id-1) {
                    area_active = area_active || scen->is_active;
                }
            }
            if (!area_active) {
                continue;
            }
            printf("check collision with Areas: %s\n", area->AreaName);
            // Assume each area is represented as an axis-aligned rectangle in the X-Z plane.
            // Compute rectangle boundaries.
            float halfWidth  = area->AreaWidth / 2.0f;
            float halfHeight = area->AreaHeight / 2.0f;
            float left   = area->position.x - halfWidth;
            float right  = area->position.x + halfWidth;
            float top    = area->position.z - halfHeight;
            float bottom = area->position.z + halfHeight;

            // Lambda to test if a point is inside the rectangle.
            auto pointInRect = [&](const Vector2D &pt) -> bool {
                return (pt.x >= left && pt.x <= right && pt.y >= top && pt.y <= bottom);
            };
            if (pointInRect(origine)) {
                // we know we are in this area already
                printf("We are in area: %s\n", area->AreaName);
                continue;
            }
            // If either endpoint is inside, we have a collision.
            /*if (pointInRect(origine) || pointInRect(destination)) {
                printf("Collision detected with area: %s\n", area->AreaName);
                continue;
            }*/

            auto doLinesIntersect = [&](const Vector2D &p1, const Vector2D &p2,
                                            const Vector2D &p3, const Vector2D &p4) -> std::optional<Vector2D> {
                float r_px = p2.x - p1.x;
                float r_py = p2.y - p1.y;
                float s_px = p4.x - p3.x;
                float s_py = p4.y - p3.y;
                float denominator = r_px * s_py - r_py * s_px;
                if (fabs(denominator) < 1e-6f)
                    return std::nullopt; // lines are parallel

                float t = ((p3.x - p1.x) * s_py - (p3.y - p1.y) * s_px) / denominator;
                float u = ((p3.x - p1.x) * r_py - (p3.y - p1.y) * r_px) / denominator;

                if (t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f) {
                    // Return the intersection point
                    return Vector2D { p1.x + t * r_px, p1.y + t * r_py };
                }
                return std::nullopt;
            };

            // Define rectangle edges (using two points per edge).
            Vector2D r1 = {left, top};
            Vector2D r2 = {right, top};
            Vector2D r3 = {right, bottom};
            Vector2D r4 = {left, bottom};

            std::optional<Vector2D> i1 = doLinesIntersect(origine, destination, r1, r2);
            std::optional<Vector2D> i2 = doLinesIntersect(origine, destination, r2, r3);
            std::optional<Vector2D> i3 = doLinesIntersect(origine, destination, r3, r4);
            std::optional<Vector2D> i4 = doLinesIntersect(origine, destination, r4, r1);

            // Check if the segment (origin,destination) intersects any rectangle edge.
            if (i1 != std::nullopt ||
                i2 != std::nullopt ||
                i3 != std::nullopt ||
                i4 != std::nullopt)
            {
                printf("Collision detected with area: %s\n", area->AreaName);
                //continue;
            }
            if (i1 != std::nullopt) {
                path.push_back(i1.value());
            }
            if (i2 != std::nullopt) {
                path.push_back(i2.value());
            }
            if (i3 != std::nullopt) {
                path.push_back(i3.value());
            }
            if (i4 != std::nullopt) {
                path.push_back(i4.value());
            }
        }
    }
    if (!path.empty()) {
        Vector2D selectedPoint;
        float minDistSq = (std::numeric_limits<float>::max)();
        for (const auto &pt : path) {
            float dx = pt.x - origine.x;
            float dy = pt.y - origine.y;
            float distSq = dx * dx + dy * dy;
            if (distSq < minDistSq) {
                minDistSq = distSq;
                selectedPoint = pt;
            }
        }
        printf("Selected point: (%.3f, %.3f)\n", selectedPoint.x, selectedPoint.y);
        
        
        this->player_plane->x = selectedPoint.x;
        this->player_plane->z = selectedPoint.y;
        
    } else {
        printf("No intersection points found in path.\n");
        
        this->player_plane->x = destination.x;
        this->player_plane->z = destination.y;
    }
    // WC3 space missions have no terrain at all (WC3Mission::loadMission()
    // only constructs `area` for ground missions — see is_space_mission),
    // so `area` can legitimately be null here even on a real, working
    // mission. SCStrike is shared with Strike Commander, which always has
    // terrain, so this null case was never hit there. No ground to clamp
    // against in space — fall straight to the dest_y branch below by
    // making the comparison a no-op (dest_y < dest_min_altitude is false
    // when they're equal).
    float dest_min_altitude = (this->current_mission->area != nullptr)
        ? this->current_mission->area->getY(this->player_plane->x, this->player_plane->z)
        : dest_y;
    if (dest_y<dest_min_altitude) {
        this->player_plane->y += dest_min_altitude;
    } else {
        this->player_plane->y = dest_y;    
    }
    
    
    
    this->player_plane->ptw.Identity();
    this->player_plane->ptw.translateM(this->player_plane->x, this->player_plane->y, this->player_plane->z);
    this->player_plane->ptw.rotateM(0, 0, 1, 0);
    this->player_plane->ptw.rotateM(0, 1, 0, 0);
    this->player_plane->ptw.rotateM(0, 0, 0, 1);
    this->player_plane->Simulate();
    Vector3D formation_pos_offset{80.0f, 0.0f, 40.0f};
    int team_number = 1;
    Vector3D prev={this->player_plane->x, this->player_plane->y, this->player_plane->z};
    for (auto team: this->current_mission->friendlies) {
        if (team->is_active) {
            if (team->plane != nullptr && team->plane != this->player_plane) {
                prev += formation_pos_offset;
                team->taken_off = true;
                team->plane->x = prev.x;
                team->plane->z = prev.z;
                team->plane->y = prev.y;
                team->plane->yaw=0;
                team->plane->pitch=0;
                team->plane->roll=0;
                team->plane->ptw.Identity();
                team->plane->ptw.translateM(team->plane->x, team->plane->y, team->plane->z);
                team->plane->Simulate();
                team_number++;
            }
        }
    }
    this->camera_mode = View::AUTO_PILOT;
    this->autopilot_timeout = 400;
}

void SCStrike::beginAutopilotSequence(AutopilotSequence kind) {
    SCMissionActors *carrier = nullptr;
    if (!this->findCarrierActor(carrier) || this->player_plane == nullptr) {
        return;
    }
    this->autopilot_seq_start_pos = {this->player_plane->x, this->player_plane->y, this->player_plane->z};
    this->autopilot_seq_start_yaw = this->player_plane->yaw;

    // Same heading convention as getDockedCarrierYawTenths/setCameraFollow
    // elsewhere in this file: tenths-of-a-degree, azimuth measured the
    // engine's own way (360-azymuth). setCameraFollow's own math confirms
    // {sin(azim), cos(azim)} is this convention's BACKWARD direction (it's
    // used with a *negative* distanceBehind to place the camera behind the
    // ship) — real forward is the negation. Bug found via live testing:
    // the ship reversed out of the hangar instead of flying forward when
    // this was originally written as +{sin,cos}.
    float carrierYawTenths = (360.0f - static_cast<float>(carrier->object->azymuth)) * 10.0f;
    float carrierYawRad = tenthOfDegreeToRad(carrierYawTenths);
    Vector3D carrierForward = {-sinf(carrierYawRad), 0.0f, -cosf(carrierYawRad)};

    if (kind == AutopilotSequence::TAKEOFF) {
        // End point: straight out along the carrier's own heading, well
        // clear of the hangar-bay proximity radius.
        this->autopilot_seq_end_pos = {
            carrier->object->position.x + carrierForward.x * (HANGAR_INTERIOR_RADIUS * 2.0f),
            carrier->object->position.y,
            carrier->object->position.z + carrierForward.z * (HANGAR_INTERIOR_RADIUS * 2.0f)
        };
    } else {
        // LANDING: end point is the carrier/bay position itself.
        this->autopilot_seq_end_pos = carrier->object->position;
    }
    this->autopilot_seq_end_yaw = carrierYawTenths;

    this->camera_mode = View::AUTO_PILOT;
    this->autopilot_timeout = 400;
    this->autopilot_sequence = kind;
}

void SCStrike::updateAutopilotSequence() {
    constexpr float kSeqRange = 400.0f + (float)AUTOPILOTE_TIMEOUT;
    float t = (400.0f - (float)this->autopilot_timeout) / kSeqRange;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    Vector3D pos = {
        this->autopilot_seq_start_pos.x + (this->autopilot_seq_end_pos.x - this->autopilot_seq_start_pos.x) * t,
        this->autopilot_seq_start_pos.y + (this->autopilot_seq_end_pos.y - this->autopilot_seq_start_pos.y) * t,
        this->autopilot_seq_start_pos.z + (this->autopilot_seq_end_pos.z - this->autopilot_seq_start_pos.z) * t
    };
    // Plain lerp — start/end are both real bounded-sequence headings, not
    // arbitrary angles, so shortest-path wraparound isn't a concern here.
    float yaw = this->autopilot_seq_start_yaw + (this->autopilot_seq_end_yaw - this->autopilot_seq_start_yaw) * t;

    this->player_plane->x = pos.x;
    this->player_plane->y = pos.y;
    this->player_plane->z = pos.z;
    this->player_plane->yaw = yaw;
    this->player_plane->ptw.Identity();
    this->player_plane->ptw.translateM(pos.x, pos.y, pos.z);
    this->player_plane->ptw.rotateM(tenthOfDegreeToRad(yaw), 0, 1, 0);
    this->player_plane->Simulate();

    // Camera: real user-observed shot descriptions (2026-07 session).
    // Neither reads AUTO's own (unconfirmed) trailing bytes — see
    // WorldCameraAutoSequence's comment in RSWorld.h.
    if (this->autopilot_sequence == AutopilotSequence::TAKEOFF && t < 0.6f) {
        // Phase A (t in [0, 0.6)): camera ahead-and-above the ship, looking
        // back/down at it, swinging down to underneath as the ship advances
        // out of the bay.
        float phaseT = t / 0.6f;
        float yawRad = tenthOfDegreeToRad(yaw);
        Vector3D shipForward = {sinf(yawRad), 0.0f, cosf(yawRad)};
        float aheadDist = 40.0f * (1.0f - phaseT);
        float heightOffset = 25.0f * (1.0f - phaseT) - 15.0f * phaseT; // +above -> -below
        Vector3D camPos = {
            pos.x - shipForward.x * aheadDist,
            pos.y + heightOffset,
            pos.z - shipForward.z * aheadDist
        };
        camera->SetPosition(&camPos);
        camera->lookAt(&pos);
    } else if (this->autopilot_sequence == AutopilotSequence::TAKEOFF) {
        // Phase B (t in [0.6, 1.0]): hard cut to the standard chase cam.
        this->setCameraFollow(this->player_plane);
    } else {
        // LANDING: camera starts behind the ship and moves toward the
        // carrier, decelerating (ease-out) to a stop at a point short of
        // the ship's own full path — the ship continues past the now-
        // nearly-stationary camera and flies on into the bay.
        float startYawRad = tenthOfDegreeToRad(this->autopilot_seq_start_yaw);
        // Real forward — see beginAutopilotSequence's comment on why
        // {sin,cos} alone is this convention's backward direction.
        Vector3D shipStartForward = {-sinf(startYawRad), 0.0f, -cosf(startYawRad)};
        Vector3D camStart = {
            this->autopilot_seq_start_pos.x - shipStartForward.x * 65.0f,
            this->autopilot_seq_start_pos.y + 10.0f,
            this->autopilot_seq_start_pos.z - shipStartForward.z * 65.0f
        };
        constexpr float kCamStopFraction = 0.6f; // camera stops short of the bay itself
        Vector3D camEnd = {
            this->autopilot_seq_start_pos.x + (this->autopilot_seq_end_pos.x - this->autopilot_seq_start_pos.x) * kCamStopFraction,
            this->autopilot_seq_start_pos.y + (this->autopilot_seq_end_pos.y - this->autopilot_seq_start_pos.y) * kCamStopFraction + 10.0f,
            this->autopilot_seq_start_pos.z + (this->autopilot_seq_end_pos.z - this->autopilot_seq_start_pos.z) * kCamStopFraction
        };
        float camT = 1.0f - (1.0f - t) * (1.0f - t); // quadratic ease-out
        Vector3D camPos = {
            camStart.x + (camEnd.x - camStart.x) * camT,
            camStart.y + (camEnd.y - camStart.y) * camT,
            camStart.z + (camEnd.z - camStart.z) * camT
        };
        camera->SetPosition(&camPos);
        camera->lookAt(&pos); // tracks the ship as it flies past into the bay
    }

    if (this->autopilot_timeout > -AUTOPILOTE_TIMEOUT) {
        this->autopilot_timeout -= AUTOPILOTE_SPEED;
        return;
    }

    // Completion: snap to the exact end transform (avoid float-lerp drift),
    // then hand off per sequence kind.
    this->player_plane->x = this->autopilot_seq_end_pos.x;
    this->player_plane->y = this->autopilot_seq_end_pos.y;
    this->player_plane->z = this->autopilot_seq_end_pos.z;
    this->player_plane->yaw = this->autopilot_seq_end_yaw;
    this->player_plane->ptw.Identity();
    this->player_plane->ptw.translateM(this->autopilot_seq_end_pos.x, this->autopilot_seq_end_pos.y, this->autopilot_seq_end_pos.z);
    this->player_plane->ptw.rotateM(tenthOfDegreeToRad(this->autopilot_seq_end_yaw), 0, 1, 0);
    this->player_plane->Simulate();

    if (this->autopilot_sequence == AutopilotSequence::LANDING) {
        // Reuses the existing manual-landing mission-end path (SCMission::
        // update()'s own landing_clearance_granted/airspeed<100 check) —
        // no duplicated end-mission logic here.
        this->player_plane->landed = true;
    }
    // TAKEOFF needs nothing else: the plane is now physically outside the
    // bay, so SCMission::update()'s existing area-crossing check picks up
    // has_left_carrier_bay=true on its own next tick.
    this->autopilot_sequence = AutopilotSequence::NONE;
    this->camera_mode = View::FRONT;
}

// Distinct gun (weapon_category==0) weapon_ids mounted on plane, in
// first-encountered order scanning weaps_load by hardpoint index — the
// ordering CYCLE_GUNS/G cycles through and FIRE_PRIMARY/Space fires
// against (see SCPlane::selected_gun_group's own comment). A ship with
// twin hardpoints of the same gun type only contributes one entry here;
// firing that type still fires every hardpoint sharing it.
static std::vector<int> collectDistinctGunTypes(SCPlane *plane) {
    std::vector<int> types;
    for (auto *hpt : plane->weaps_load) {
        if (hpt == nullptr || hpt->objct == nullptr || hpt->objct->wdat == nullptr) {
            continue;
        }
        if (hpt->objct->wdat->weapon_category != 0) {
            continue;
        }
        int weaponId = hpt->objct->wdat->weapon_id;
        if (std::find(types.begin(), types.end(), weaponId) == types.end()) {
            types.push_back(weaponId);
        }
    }
    return types;
}
// Backs selected_missile_group, WC3's own independent missile-*bank* cycle
// (see that field's own comment for why this can't just reuse
// selected_weapon/MDFS_WEAPONS's existing SC1-oriented logic). Each
// missile/ordnance (weapon_category != 0) hardpoint is its own bank —
// user-corrected, 2026-07 session: "only one missile bank is selected at
// a time, regardless if it has the same type of missiles as another
// bank" — so this collects weaps_load *indices*, not distinct weapon_ids
// (an earlier version deduped by type, which put two same-type banks
// under the same cycle step and selected/fired both together). Guns
// still intentionally cycle by type (see CYCLE_GUNS's own comment — every
// hardpoint sharing that type fires as one simultaneous salvo, a
// deliberate, different mechanic from missiles' one-bank-at-a-time rule).
static std::vector<int> collectMissileHardpointIndices(SCPlane *plane) {
    std::vector<int> indices;
    for (size_t i = 0; i < plane->weaps_load.size(); i++) {
        auto *hpt = plane->weaps_load[i];
        if (hpt == nullptr || hpt->objct == nullptr || hpt->objct->wdat == nullptr) {
            continue;
        }
        if (hpt->objct->wdat->weapon_category == 0) {
            continue;
        }
        indices.push_back((int)i);
    }
    return indices;
}

/**
 * @brief Handle keyboard events
 *
 * This function peeks at the SDL event queue to check for keyboard events.
 * If a key is pressed, it updates the game state based on the key pressed.
 * The supported keys are escape, space, and return.
 *
 * @return None
 */
void SCStrike::checkKeyboard(void) {
    int msx = 0;
    int msy = 0;
    
    float dx = m_keyboard->getActionValue(CreateAction(InputAction::SIM_START, SimActionOfst::MOUSE_X));
    float dy = m_keyboard->getActionValue(CreateAction(InputAction::SIM_START, SimActionOfst::MOUSE_Y));

    msy = dy - (Screen->logical_height / 2);
    msx = dx - (Screen->logical_width / 2);
    //Mouse.setPosition({(int)dx, (int)dy});
    if (this->camera_mode == View::AUTO_PILOT) {
        return;
    }
    if (!this->autopilot) {
        if (this->mouse_control) {
            this->player_plane->control_stick_x = msx;
            this->player_plane->control_stick_y = msy;
        } else {
            this->player_plane->control_stick_x = 0;
            this->player_plane->control_stick_y = 0;
        }
    }
    if (this->camera_mode == View::REAL) {
        this->pilote_lookat.x = ((Screen->width / 360) * msx) / 6;
        this->pilote_lookat.y = ((Screen->height / 360) * msy) / 6;
    }
    bool is_rudder_pressed = false;
    if (m_keyboard->isActionPressed(CreateAction(InputAction::SIM_START, SimActionOfst::RUDDER_LEFT))) {
        this->player_plane->rudder -= 0.1f;
        is_rudder_pressed = true;
        // Unlike control_stick_x/y (roll/pitch), rudder never disabled
        // mouse control — meaning stray mouse movement could still drive
        // control_stick_x (roll) at the same time as a yaw key press,
        // looking like the axes were swapped even though they weren't.
        this->mouse_control = false;
    }
    if (m_keyboard->isActionPressed(CreateAction(InputAction::SIM_START, SimActionOfst::RUDDER_RIGHT))) {
        this->player_plane->rudder += 0.1f;
        is_rudder_pressed = true;
        this->mouse_control = false;
    }
    bool is_shift_held = m_keyboard->isActionPressed(CreateAction(InputAction::SIM_START, SimActionOfst::MODIFIER_SHIFT));
    if (!is_rudder_pressed) {
        this->player_plane->rudder = 0;
    } else {
        // Shift held = full-rate yaw, otherwise half-rate — matches the
        // half/full speed keyboard scheme for every rotation axis.
        float rudderCap = is_shift_held ? 10.0f : 5.0f;
        this->player_plane->rudder = std::clamp(this->player_plane->rudder, -rudderCap, rudderCap);
    }
    if (m_keyboard->isActionPressed(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_UP))) {
        if (this->player_plane->GetThrottle() == 0) {
            this->player_plane->SetThrottle(21);
            if (this->current_mission->sound.sounds.size() > 0) {
                MemSound *engine = this->current_mission->sound.sounds[SoundEffectIds::ENGINE_START_MIL];
                Mixer.playSoundVoc(engine->data, engine->size, 5, 0);
            }
        }
        if (this->player_plane->GetThrottle() < 100) {
            this->player_plane->SetThrottle(this->player_plane->GetThrottle() + 1);
        }
        if (this->current_mission->sound.sounds.size() > 0) {
            if (!Mixer.isSoundPlaying(5)) {
                if (this->player_plane->GetThrottle() > 0 && this->player_plane->GetThrottle() < 60) {
                    MemSound *engine = this->current_mission->sound.sounds[SoundEffectIds::ENGINE_MIL];
                    Mixer.playSoundVoc(engine->data, engine->size, 5, -1);
                } else if (this->player_plane->GetThrottle() >= 60) {
                    MemSound *engine = this->current_mission->sound.sounds[SoundEffectIds::ENGINE_AFB];
                    Mixer.playSoundVoc(engine->data, engine->size, 5, -1);
                }
            }
        }
    }
    
    if (m_keyboard->isActionPressed(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_DOWN))) {
        if (this->player_plane->GetThrottle() > -30) {
            this->player_plane->SetThrottle(this->player_plane->GetThrottle() - 1);
        }
        if (this->player_plane->GetThrottle() == 0) {
            if (!Mixer.isSoundPlaying(5)) {
                if (this->player_plane->GetThrottle() > 0 && this->player_plane->GetThrottle() < 60) {
                    MemSound *engine = this->current_mission->sound.sounds[SoundEffectIds::ENGINE_MIL];
                    Mixer.playSoundVoc(engine->data, engine->size, 5, -1);
                } else if (this->current_mission->sound.sounds.size() > 0) {
                    MemSound *engine = this->current_mission->sound.sounds[SoundEffectIds::ENGINE_MIL_SHUT_DOWN];
                    Mixer.playSoundVoc(engine->data, engine->size, 5, 0);
                }
            }

        }
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_STOP))) {
        this->player_plane->SetThrottle(0);
        if (this->current_mission->sound.sounds.size() > 0 && !Mixer.isSoundPlaying(5)) {
            MemSound *engine = this->current_mission->sound.sounds[SoundEffectIds::ENGINE_MIL_SHUT_DOWN];
            Mixer.playSoundVoc(engine->data, engine->size, 5, 0);
        }
    }
    this->player_plane->afterburner_engaged =
        m_keyboard->isActionPressed(CreateAction(InputAction::SIM_START, SimActionOfst::AFTERBURNER));
    this->updateAfterburnerSound(this->player_plane->afterburner_engaged);
    this->cockpit->is_shooting = false;
    // Real WC-series controls: dedicated gun/missile triggers, not one fire
    // button cycling through every hardpoint type (see FIRE_MISSILE's own
    // comment in GameEngine.h). CYCLE_GUNS/G selects which gun TYPE
    // FIRE_PRIMARY/Space fires — N distinct types means N+1 states (each
    // type individually, then a final "fire every gun together" state).
    // Guarded against Ctrl held: WC3's Ctrl+G (gun-sync toggle, see
    // WC3Strike::checkGunSyncToggleInput) shares the bare G scancode with
    // this binding, and isActionJustPressed doesn't know about modifiers —
    // without this guard, Ctrl+G would both cycle guns and toggle sync.
    if (!(SDL_GetModState() & KMOD_CTRL) &&
        m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::CYCLE_GUNS))) {
        std::vector<int> gunTypes = collectDistinctGunTypes(this->player_plane);
        if (!gunTypes.empty()) {
            this->player_plane->selected_gun_group =
                (this->player_plane->selected_gun_group + 1) % ((int)gunTypes.size() + 1);
            printf("CYCLE_GUNS: %zu distinct gun type(s) found, now selecting group %d%s\n",
                   gunTypes.size(), this->player_plane->selected_gun_group,
                   (this->player_plane->selected_gun_group == (int)gunTypes.size()) ? " (all guns)" : "");
        } else {
            printf("CYCLE_GUNS: pressed, but weaps_load has zero gun-category (weapon_category==0) hardpoints with a resolved weapon -- nothing to cycle\n");
        }
        // User-requested (2026-07 session): G should also open the
        // weapons MFD, same as CYCLE_MISSILES/M does below.
        this->cockpit->show_weapons = true;
    }
    // M cycles WC3's independent missile/ordnance-*bank* selection
    // (SCPlane::selected_missile_group), completely separate from
    // CYCLE_GUNS/gun-type selection above — user-requested (2026-07
    // session), its own action now rather than an MDFS_WEAPONS side
    // effect (see that action's own comment). Only meaningful for WC3
    // ships; SC1's selected_weapon index has no per-bank concept to
    // cycle. Mirrors CYCLE_GUNS's own "also open the weapons MFD"
    // behavior.
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::CYCLE_MISSILES)) &&
        this->player_plane->is_wc3_ship) {
        std::vector<int> bankIndices = collectMissileHardpointIndices(this->player_plane);
        if (!bankIndices.empty()) {
            this->player_plane->selected_missile_group =
                (this->player_plane->selected_missile_group + 1) % (int)bankIndices.size();
        }
        this->player_plane->wp_cooldown = 0;
        if (!this->cockpit->show_weapons) {
            this->cockpit->ClearLeftMfdPages();
        }
        this->cockpit->show_weapons = true;
    }
    // Guns always fire down the boresight, no target required. Fires every
    // hardpoint matching the CYCLE_GUNS-selected type (or every gun
    // hardpoint at all, in the final "all guns" state) as one simultaneous
    // salvo. wp_cooldown is a single value shared across the whole plane,
    // not per-hardpoint — Shoot() itself owns the "still on cooldown"
    // decrement-and-return logic, so the *first* candidate hardpoint is
    // always called normally (this is what lets wp_cooldown actually tick
    // down frame to frame; gating the loop itself on wp_cooldown<=0
    // without ever calling Shoot() was a real bug — it froze wp_cooldown
    // permanently at whatever value the first successful shot set, since
    // nothing ever called Shoot() again to decrement it). Only once that
    // first call confirms a real fire this frame (via its bool return) do
    // later hardpoints in the same salvo get their cooldown forced to 0 so
    // they aren't blocked by the first one's freshly-set value.
    if (m_keyboard->isActionPressed(CreateAction(InputAction::SIM_START, SimActionOfst::FIRE_PRIMARY))) {
        std::vector<int> gunTypes = collectDistinctGunTypes(this->player_plane);
        if (!gunTypes.empty()) {
            bool fireAll = (this->player_plane->selected_gun_group >= (int)gunTypes.size());
            int targetWeaponId = fireAll ? -1 : gunTypes[this->player_plane->selected_gun_group];
            std::vector<size_t> matchingHpts;
            for (size_t i = 0; i < this->player_plane->weaps_load.size(); i++) {
                auto *hpt = this->player_plane->weaps_load[i];
                if (hpt == nullptr || hpt->objct == nullptr || hpt->objct->wdat == nullptr) {
                    continue;
                }
                if (hpt->objct->wdat->weapon_category != 0) {
                    continue;
                }
                if (!fireAll && hpt->objct->wdat->weapon_id != targetWeaponId) {
                    continue;
                }
                matchingHpts.push_back(i);
            }
            if (!matchingHpts.empty()) {
                // When energy can't cover firing every matching hardpoint
                // together this press, alternate through them one at a time
                // instead of always attempting the same hardpoint first
                // (SCPlane::Shoot()'s own energy check would otherwise let
                // hardpoint 0 drain what little energy there is every time,
                // starving the others). Only applies to WC3's energy-gated
                // guns — SC1's ID_20MM and anything without a resolved wdat
                // always fire every matching hardpoint together.
                std::vector<size_t> hptsToFire = matchingHpts;
                RSEntity *firstObjct = this->player_plane->weaps_load[matchingHpts[0]]->objct;
                if (matchingHpts.size() > 1 && firstObjct->wdat != nullptr &&
                    SCPlane::IsWC3EnergyGunWeaponId(firstObjct->wdat->weapon_id)) {
                    float energyCostEach = (firstObjct->wdat->energy_cost > 0) ? (float)firstObjct->wdat->energy_cost : 5.0f;
                    float currentEnergy = this->player_plane->GetCurrentGunEnergy();
                    if (currentEnergy < energyCostEach * (float)matchingHpts.size()) {
                        size_t pick = matchingHpts[this->player_plane->gun_alternate_index % matchingHpts.size()];
                        this->player_plane->gun_alternate_index++;
                        hptsToFire = {pick};
                    }
                }
                // WC3's real default is desynchronized gun-pair fire: one
                // cannon fires slightly before the other, picked at random
                // each shot, the leader ending up about half a bolt's length
                // ahead — toggled off (both fire in perfect sync) with
                // Ctrl+G (SCPlane::guns_synchronized). Implemented as a
                // spawn-position head start rather than an actual timing
                // delay (see Shoot()'s spawnAdvanceDistance param comment)
                // since a real delay would be a few milliseconds, well under
                // this simulation's own tick granularity.
                // kBoltLength must track SCRenderer::drawBolt's own geometry
                // (8.9+8.5 unscaled) and its glScalef call — both currently
                // 1.5x.
                constexpr float kBoltLength = (8.9f + 8.5f) * 1.5f;
                int leaderPick = -1;
                if (hptsToFire.size() > 1 && !this->player_plane->guns_synchronized) {
                    leaderPick = (int)(std::rand() % hptsToFire.size());
                }
                // Only the first hardpoint to fire is called unconditionally
                // (so a not-yet-ready cooldown only ticks down once per
                // frame, same as before this salvo logic existed) — the rest
                // only fire if that first call actually fired.
                float advance0 = (leaderPick == 0) ? kBoltLength * 0.5f : 0.0f;
                bool fired = this->player_plane->Shoot((int)hptsToFire[0], target, this->current_mission, 999.0f, advance0);
                if (fired) {
                    this->cockpit->is_shooting = true;
                    for (size_t idx = 1; idx < hptsToFire.size(); idx++) {
                        this->player_plane->wp_cooldown = 0;
                        float advance = (leaderPick == (int)idx) ? kBoltLength * 0.5f : 0.0f;
                        this->player_plane->Shoot((int)hptsToFire[idx], target, this->current_mission, 999.0f, advance);
                    }
                }
            }
        }
    }
    // Missiles/torpedo (Enter/FIRE_MISSILE) fire whatever MDFS_WEAPONS has
    // cycled selected_weapon to — still requires a target (guidance/lock
    // need something to aim at), unlike the gun above. Guarded against
    // selected_weapon landing on a gun slot (Enter shouldn't double as a
    // second gun trigger).
    if (m_keyboard->isActionPressed(CreateAction(InputAction::SIM_START, SimActionOfst::FIRE_MISSILE))) {
        // WC3: resolve the hardpoint(s) from selected_missile_group's own
        // independent missile-type cycle instead of the SC1 selected_weapon
        // index (which MDFS_WEAPONS no longer drives for WC3 ships at all —
        // see that handler's own comment). selected_missile_group < 0 is
        // the "select all hardpoints" state (B key, user-confirmed 2026-07
        // session — WC3 has no airbrake to steal B for, unlike Strike
        // Commander): every missile/torpedo hardpoint fires together,
        // rather than resolving to the first one matching a single type.
        std::vector<int> selIdxs;
        if (this->player_plane->is_wc3_ship) {
            if (this->player_plane->selected_missile_group < 0) {
                for (size_t i = 0; i < this->player_plane->weaps_load.size(); i++) {
                    auto *hpt = this->player_plane->weaps_load[i];
                    if (hpt != nullptr && hpt->objct != nullptr && hpt->objct->wdat != nullptr &&
                        hpt->objct->wdat->weapon_category != 0) {
                        selIdxs.push_back((int)i);
                    }
                }
            } else {
                std::vector<int> bankIndices = collectMissileHardpointIndices(this->player_plane);
                if (!bankIndices.empty()) {
                    selIdxs.push_back(bankIndices[this->player_plane->selected_missile_group % bankIndices.size()]);
                }
            }
        } else {
            selIdxs.push_back(this->player_plane->selected_weapon);
        }
        // wp_cooldown is shared across the whole plane, not per-hardpoint
        // (same as the gun salvo above) — only the first hardpoint to fire
        // this frame is called unconditionally; later ones in the same
        // "all hardpoints" salvo get their cooldown forced to 0 first so
        // the first shot's freshly-set cooldown doesn't block them too.
        bool firstFired = false;
        bool anyAttempted = false;
        for (int selIdx : selIdxs) {
            if (selIdx < 0 || (size_t)selIdx >= this->player_plane->weaps_load.size()) {
                continue;
            }
            auto *selected = this->player_plane->weaps_load[selIdx];
            if (selected == nullptr || selected->objct == nullptr || selected->objct->wdat == nullptr ||
                selected->objct->wdat->weapon_category == 0) {
                continue;
            }
            // WC3's heatseeker/image-recognition/friend-or-foe missiles
            // can dumbfire (fly straight ahead, unguided) with no target
            // selected at all — see SCPlane::Shoot()'s own comment on
            // this. Requiring `target != nullptr` here unconditionally
            // (as before) silently blocked FIRE_MISSILE from doing
            // anything at all whenever nothing was targeted, even
            // though Shoot()'s own dumbfire branch already handles a
            // null target safely. Torpedo/T-bomb (weapon_category==3)
            // and SC1's own missiles still require a real target, same
            // as before — only these three IDs get the relaxation.
            int weaponId = selected->objct->wdat->weapon_id;
            bool canDumbfireUnlocked = weaponId == weapon_ids::ID_HSMISS ||
                weaponId == weapon_ids::ID_IRMISS || weaponId == weapon_ids::ID_FFMISS;
            if (target == nullptr && !canDumbfireUnlocked) {
                continue;
            }
            // Real lock-on timer (SCCockpit::updateLockOn) — lets Shoot()
            // decide missile guided-vs-dumbfire and gate torpedo/T-bomb
            // firing entirely.
            if (!anyAttempted) {
                firstFired = this->player_plane->Shoot((int)selIdx, target, this->current_mission,
                                                         this->cockpit->lock_progress);
                anyAttempted = true;
                if (firstFired) {
                    this->cockpit->is_shooting = true;
                }
            } else if (firstFired) {
                this->player_plane->wp_cooldown = 0;
                this->player_plane->Shoot((int)selIdx, target, this->current_mission,
                                           this->cockpit->lock_progress);
            }
        }
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::TOGGLE_MOUSE))) {
        this->mouse_control = !this->mouse_control;
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::AUTOPILOT))) {
        // checkKeyboard() already returns early (see the top of this
        // function) whenever camera_mode==View::AUTO_PILOT, so this can't
        // re-trigger/reset a sequence that's already playing.
        if (!this->current_mission->has_left_carrier_bay) {
            this->beginAutopilotSequence(AutopilotSequence::TAKEOFF);
        } else if (this->current_mission->landing_clearance_granted) {
            this->beginAutopilotSequence(AutopilotSequence::LANDING);
        } else if (this->CanEngageAutopilot()) {
            this->autopilotCompute();
        }
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::LANDING_GEAR))) {
        this->player_plane->SetWheel();
        if (this->current_mission->sound.sounds.size() > 0) {
            if (this->player_plane->GetWheel()) {
                MemSound *gears = this->current_mission->sound.sounds[SoundEffectIds::GEARS_UP];
                Mixer.playSoundVoc(gears->data, gears->size, 4, 0);
            } else {
                MemSound *gears = this->current_mission->sound.sounds[SoundEffectIds::GEARS_DOWN];
                Mixer.playSoundVoc(gears->data, gears->size, 4, 0);
            }
        }
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::TOGGLE_BRAKES))) {
        if (this->player_plane->is_wc3_ship) {
            // WC3 has no airbrake (user-confirmed, 2026-07 session: "wing
            // commander hasn't got brakes like Strike Commander") — B is
            // instead "select all hardpoints", selecting every missile/
            // torpedo hardpoint to fire simultaneously (see
            // selected_missile_group's own comment for the sentinel) —
            // and opens the weapons MFD, same as CYCLE_GUNS/MDFS_WEAPONS.
            this->player_plane->selected_missile_group = -1;
            this->cockpit->show_weapons = true;
        } else {
            this->player_plane->SetSpoilers();
        }
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::TOGGLE_FLAPS))) {
        this->player_plane->SetFlaps();
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::TARGET_NEAREST))) {
        this->findTarget();
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::LOCK_TARGET))) {
        if (this->target != nullptr) {
            this->cockpit->target_hard_locked = !this->cockpit->target_hard_locked;
        }
    }
    // Toggles this->cockpit->show_radars — only consulted for Strike
    // Commander's own (non-WC3) radar page now (SCCockpit.cpp's runFrame
    // page-rotation call site). WC3's radar always draws unconditionally
    // (user-confirmed 2026-07 session: no toggle key for it), so this key
    // press is a no-op for WC3 cockpits rather than removed outright —
    // SCStrike.cpp is shared between both games, and SC still needs it.
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::MDFS_RADAR))) {
        if (!this->cockpit->show_radars) {
            this->cockpit->ClearLeftMfdPages();
            this->cockpit->show_radars = true;
        } else {
            this->cockpit->show_radars = false;
        }
        // WC3's real "cycle target sub-system/turret" key. Reuses the same
        // 'r' binding show_radars is already dead code for under WC3 (see
        // comment above) instead of a separate action — a plain no-op for
        // SC/any target with no turret data (turretCount == 0).
        int turretCount = this->cockpit->GetCurrentTargetTurretCount();
        if (turretCount > 0) {
            this->cockpit->targeted_turret_index = (this->cockpit->targeted_turret_index + 1) % turretCount;
        }
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::MDFS_DAMAGE))) {
        // Damage MFD has two real pages (user-described, 2026-07 session:
        // subsystem text list, then a ship-graphic damage diagram) — see
        // SCCockpit::damage_page. First press opens the panel on page 0;
        // pressing again while it's open cycles to the next page; pressing
        // once more (wrapping back to page 0) closes it.
        if (!this->cockpit->show_damage) {
            this->cockpit->ClearLeftMfdPages();
            this->cockpit->show_damage = true;
            this->cockpit->damage_page = 0;
        } else {
            this->cockpit->damage_page = (this->cockpit->damage_page + 1) % 2;
            if (this->cockpit->damage_page == 0) {
                this->cockpit->show_damage = false;
            }
        }
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::MDFS_POWER))) {
        // User-confirmed real behavior: 'p' rotates E->W->S->D->shield->E
        // rather than a plain open/close toggle. See CyclePowerOrShield.
        this->cockpit->CyclePowerOrShield();
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::MDFS_SHIELD))) {
        // User-requested (2026-07 session): pressing 's' while already on
        // the shield page is a no-op now — it no longer closes the page.
        if (!this->cockpit->show_shield) {
            this->cockpit->ClearLeftMfdPages();
            this->cockpit->show_shield = true;
        }
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::CYCLE_COCKPIT_VIEW))) {
        this->cockpit->CycleViewMode();
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::MDFS_WEAPONS))) {
        // User-requested (2026-07 session): "pressing w should not change
        // missile or gun mounts. It should just display the weapons MFD."
        // Cycling gun type/missile bank now lives solely on their own
        // dedicated real-WC3 controls (CYCLE_GUNS/G, TOGGLE_BRAKES/B "select
        // all hardpoints" — see their own comments); W/MDFS_WEAPONS no
        // longer doubles as a cycle key for either, on WC3 or SC1 ships. No
        // effect if the weapons page is already open, same as MDFS_SHIELD.
        if (!this->cockpit->show_weapons) {
            this->cockpit->ClearLeftMfdPages();
            this->cockpit->show_weapons = true;
        }
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::SHOW_NAVMAP))) {
        SCNavMap *nav_screen = this->createNavMap();
        nav_screen->init();
        nav_screen->SetName((char *)this->current_mission->world->tera.c_str());
        nav_screen->mission = this->current_mission;
        nav_screen->missionObj = this->current_mission->mission = this->current_mission->mission;
        nav_screen->current_nav_point = &this->nav_point_id;
        Game->addActivity(nav_screen);
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::CHAFF))) {
        //this->player_plane->LaunchChaff(this->current_mission);
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::FLARE))) {
        //this->player_plane->LaunchFlares(this->current_mission);
    }
    // '[' / ']' are context-sensitive, same precedent as the M/W dual-use
    // MDFS_WEAPONS key: while the power MFD is open they nudge the
    // selected gauge (user-confirmed real binding — '[' decreases, ']'
    // increases), otherwise they keep their existing radar-zoom function.
    static constexpr float kPowerStep = 5.0f;
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::RADAR_ZOOM_IN))) {
        if (this->cockpit->show_power) {
            this->cockpit->AdjustSelectedPower(-kPowerStep);
        } else {
            this->cockpit->radar_zoom -= 1;
            if (this->cockpit->radar_zoom < 1) {
                this->cockpit->radar_zoom = 1;
            }
        }
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::RADAR_ZOOM_OUT))) {
        if (this->cockpit->show_power) {
            this->cockpit->AdjustSelectedPower(kPowerStep);
        } else {
            this->cockpit->radar_zoom += 1;
            if (this->cockpit->radar_zoom > 4) {
                this->cockpit->radar_zoom = 4;
            }
        }
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::COMM_RADIO))) {
        // User-requested (2026-07 session): pressing 'c' while already on
        // the comm page is a no-op now — it no longer closes the page.
        // Same treatment as MDFS_SHIELD/MDFS_WEAPONS.
        if (!this->cockpit->show_comm) {
            this->cockpit->ClearLeftMfdPages();
            this->cockpit->show_comm = true;
        }
    }
    
    
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::VIEW_TARGET))) {
        this->camera_mode = View::TARGET;
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::VIEW_BEHIND))) {
        if (this->camera_mode != View::FOLLOW) {
            this->follow_dynamic = false;
        }
        if (this->camera_mode == View::FOLLOW) {
            this->follow_dynamic = !this->follow_dynamic;
        }
        this->camera_mode = View::FOLLOW;
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::VIEW_COCKPIT))) {
        this->mouse_control = false;
        this->zoom_cockpit = false;
        this->camera_mode = View::REAL;
        this->cockpit->is_3d_cockpit = true;
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::RADAR_MODE_TOGGLE))) {
        if (this->cockpit->radar_mode == RadarMode::AARD) {
            this->cockpit->radar_mode = RadarMode::AGRD;
        } else if (this->cockpit->radar_mode == RadarMode::AGRD) {
            this->cockpit->radar_mode = RadarMode::AARD;
        }
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::VIEW_WEAPONS))) {
        if (this->camera_mode != View::MISSILE_CAM) {
            this->camera_mode = View::MISSILE_CAM;
        } else {
            this->camera_mode = View::REAL;
        }
    }
    // Real WC3 "Track Camera" (F10, WRLD>CAMR>TRAK). Anchors a world-fixed
    // camera at the player's position at the moment of engaging (not
    // re-anchored on every press while already in this mode, so it stays a
    // real flyby shot rather than snapping to the player each frame) —
    // see runFrame's View::TRACK case for the continuous lookAt.
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::VIEW_TRACK))) {
        if (this->camera_mode != View::TRACK) {
            const float lateral = 80.0f;
            const float above = 20.0f;
            float r_azim = tenthOfDegreeToRad(this->player_plane->azimuthf + 900.0f);
            this->track_camera_anchor = {
                this->player_plane->x + lateral * sinf(r_azim),
                this->player_plane->y + above,
                this->player_plane->z + lateral * cosf(r_azim)
            };
            this->camera_mode = View::TRACK;
        } else {
            this->camera_mode = View::REAL;
        }
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::LOOK_FORWARD))) {
        this->camera_mode = View::FRONT;
        this->cockpit->is_3d_cockpit = false;
        zoom_cockpit = !zoom_cockpit;
        this->pilote_lookat.x = 0;
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::LOOK_LEFT))) {
        this->camera_mode = View::LEFT;
        this->pilote_lookat.x = 270;
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::LOOK_RIGHT))) {
        this->camera_mode = View::RIGHT;
        this->pilote_lookat.x = 90;
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::LOOK_BEHIND))) {
        this->camera_mode = View::REAR;
        this->pilote_lookat.x = 180;
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::MDFS_TARGET_CAMERA))) {
        if (!this->cockpit->show_cam) {
            this->cockpit->ClearLeftMfdPages();
            this->cockpit->show_cam = true;
        } else {
            this->cockpit->show_cam = false;
        }
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::WEAPON_MODE_TOGGLE))) {
        this->air_weapons_mode = !this->air_weapons_mode;
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::INFINIT_AMMO_TOGGLE))) {
        this->player_plane->infinite_ammo = !this->player_plane->infinite_ammo;
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::SINGLE_TARGET_MODE))) {
        if (this->cockpit->radar_mode != RadarMode::ASST && this->target != nullptr) {
            this->cockpit->radar_mode = RadarMode::ASST;
        } else {
            this->cockpit->radar_mode = RadarMode::AARD;
        }
    }
    if (!this->cockpit->show_comm) {
        if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_10))) {
            this->player_plane->SetThrottle(10);
            if (this->current_mission->sound.sounds.size() > 0) {
                MemSound *engine = this->current_mission->sound.sounds[SoundEffectIds::ENGINE_MIL];
                Mixer.playSoundVoc(engine->data, engine->size, 5, -1);
            }
        }
        if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_20))) {
            this->player_plane->SetThrottle(20);
            if (this->current_mission->sound.sounds.size() > 0) {
                MemSound *engine = this->current_mission->sound.sounds[SoundEffectIds::ENGINE_MIL];
                Mixer.playSoundVoc(engine->data, engine->size, 5, -1);
            }
        }
        if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_30))) {
            this->player_plane->SetThrottle(30);
            if (this->current_mission->sound.sounds.size() > 0) {
                MemSound *engine = this->current_mission->sound.sounds[SoundEffectIds::ENGINE_MIL];
                Mixer.playSoundVoc(engine->data, engine->size, 5, -1);
            }
        }
        if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_40))) {
            this->player_plane->SetThrottle(40);
            if (this->current_mission->sound.sounds.size() > 0) {
                MemSound *engine = this->current_mission->sound.sounds[SoundEffectIds::ENGINE_MIL];
                Mixer.playSoundVoc(engine->data, engine->size, 5, -1);
            }
        }
        if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_50))) {
            this->player_plane->SetThrottle(50);
            if (this->current_mission->sound.sounds.size() > 0) {
                MemSound *engine = this->current_mission->sound.sounds[SoundEffectIds::ENGINE_MIL];
                Mixer.playSoundVoc(engine->data, engine->size, 5, -1);
            }
        }
    }
    if (this->cockpit->show_comm) {
        if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::COMM_RADIO_M1))) {
            if (this->cockpit->comm_target == 0) {
                this->cockpit->SetCommActorTarget(1);
            } else {
                // send message 1 to ai comm_target
                if (this->cockpit->comm_actor != nullptr) {
                    this->cockpit->comm_actor->respondToRadioMessage(1, this->current_mission, this->current_mission->player);
                    this->cockpit->show_comm = false;
                }
                this->cockpit->comm_target = 0;
            }
        }
        if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::COMM_RADIO_M2))) {
            if (this->cockpit->comm_target == 0) {
                this->cockpit->SetCommActorTarget(2);
            } else {
                if (this->cockpit->comm_actor != nullptr) {
                    this->cockpit->comm_actor->respondToRadioMessage(2, this->current_mission, this->current_mission->player);
                    this->cockpit->show_comm = false;
                }
                this->cockpit->comm_target = 0;
            }
        }
        if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::COMM_RADIO_M3))) {
            if (this->cockpit->comm_target == 0) {
                this->cockpit->SetCommActorTarget(3);
            } else {
                if (this->cockpit->comm_actor != nullptr) {
                    this->cockpit->comm_actor->respondToRadioMessage(3, this->current_mission, this->current_mission->player);
                    this->cockpit->show_comm = false;
                }
                this->cockpit->comm_target = 0;
            }
        }
        if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::COMM_RADIO_M4))) {
            if (this->cockpit->comm_target == 0) {
                this->cockpit->SetCommActorTarget(4);
            } else {
                if (this->cockpit->comm_actor != nullptr) {
                    this->cockpit->comm_actor->respondToRadioMessage(4, this->current_mission, this->current_mission->player);
                    this->cockpit->show_comm = false;
                }
                this->cockpit->comm_target = 0;
            }
        }
        if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::COMM_RADIO_M5))) {
            if (this->cockpit->comm_target == 0) {
                this->cockpit->SetCommActorTarget(5);
            } else {
                if (this->cockpit->comm_actor != nullptr) {
                    this->cockpit->comm_actor->respondToRadioMessage(5, this->current_mission, this->current_mission->player);
                    this->cockpit->show_comm = false;
                }
                this->cockpit->comm_target = 0;
            }
        }
        if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::COMM_RADIO_M6))) {
            if (this->cockpit->comm_target == 0) {
                this->cockpit->SetCommActorTarget(6);
            } else {
                if (this->cockpit->comm_actor != nullptr) {
                    this->cockpit->comm_actor->respondToRadioMessage(6, this->current_mission, this->current_mission->player);
                    this->cockpit->show_comm = false;
                }
                this->cockpit->comm_target = 0;
            }
        }
        if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::COMM_RADIO_M7))) {
            if (this->cockpit->comm_target == 0) {
                this->cockpit->SetCommActorTarget(7);
            } else {
                if (this->cockpit->comm_actor != nullptr) {
                    this->cockpit->comm_actor->respondToRadioMessage(7, this->current_mission, this->current_mission->player);
                    this->cockpit->show_comm = false;
                }
                this->cockpit->comm_target = 0;
            }
        }
        if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::COMM_RADIO_M8))) {
            if (this->cockpit->comm_target == 0) {
                this->cockpit->SetCommActorTarget(8);
            } else {
                if (this->cockpit->comm_actor != nullptr) {
                    this->cockpit->comm_actor->respondToRadioMessage(8, this->current_mission, this->current_mission->player);
                    this->cockpit->show_comm = false;
                }
                this->cockpit->comm_target = 0;
            }
        }   
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_60))) {
        this->player_plane->SetThrottle(60);
        if (this->current_mission->sound.sounds.size() > 0) {
            MemSound *engine = this->current_mission->sound.sounds[SoundEffectIds::ENGINE_AFB];
            Mixer.playSoundVoc(engine->data, engine->size, 5, -1);
        }
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_70))) {
        this->player_plane->SetThrottle(70);
        if (this->current_mission->sound.sounds.size() > 0) {
            MemSound *engine = this->current_mission->sound.sounds[SoundEffectIds::ENGINE_AFB];
            Mixer.playSoundVoc(engine->data, engine->size, 5, -1);
        }
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_80))) {
        this->player_plane->SetThrottle(80);
        if (this->current_mission->sound.sounds.size() > 0) {
            MemSound *engine = this->current_mission->sound.sounds[SoundEffectIds::ENGINE_AFB];
            Mixer.playSoundVoc(engine->data, engine->size, 5, -1);
        }
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_90))) {
        this->player_plane->SetThrottle(90);
        if (this->current_mission->sound.sounds.size() > 0) {
            MemSound *engine = this->current_mission->sound.sounds[SoundEffectIds::ENGINE_AFB];
            Mixer.playSoundVoc(engine->data, engine->size, 5, -1);
        }
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::THROTTLE_100))) {
        this->player_plane->SetThrottle(100);
        if (this->current_mission->sound.sounds.size() > 0) {
            MemSound *engine = this->current_mission->sound.sounds[SoundEffectIds::ENGINE_AFB];
            Mixer.playSoundVoc(engine->data, engine->size, 5, -1);
        }
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::PAUSE))) {
        this->pause_simu = !this->pause_simu;
    }
    // Shift held = full-rate, otherwise half-rate, matching every other
    // rotation axis (is_shift_held computed above for rudder/yaw). 200 is
    // SCJdynPlane::processInput's own kStickFullDeflection — using it here
    // too (rather than the old, inconsistent 150 for pitch) means "full"
    // actually reaches the ship's calibrated max dps, not 75% of it.
    float pitchRollDeflection = is_shift_held ? 200 : 100;
    if (m_keyboard->isActionPressed(CreateAction(InputAction::SIM_START, SimActionOfst::PITCH_UP))) {
        this->player_plane->control_stick_y = -pitchRollDeflection;
        this->mouse_control = false;
    }
    if (m_keyboard->isActionPressed(CreateAction(InputAction::SIM_START, SimActionOfst::PITCH_DOWN))) {
        this->player_plane->control_stick_y = pitchRollDeflection;
        this->mouse_control = false;
    }
    if (m_keyboard->isActionPressed(CreateAction(InputAction::SIM_START, SimActionOfst::ROLL_LEFT))) {
        this->player_plane->control_stick_x = -pitchRollDeflection;
        this->mouse_control = false;
    }
    if (m_keyboard->isActionPressed(CreateAction(InputAction::SIM_START, SimActionOfst::ROLL_RIGHT))) {
        this->player_plane->control_stick_x = pitchRollDeflection;
        this->mouse_control = false;
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::EYES_ON_TARGET))) {
        if (this->camera_mode != View::EYE_ON_TARGET) {
            this->camera_mode = View::EYE_ON_TARGET;
            this->cockpit->is_3d_cockpit = true;
        } else {
            this->camera_mode = View::FRONT;
            this->cockpit->is_3d_cockpit = false;
        }
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::END_MISSION))) {
        this->current_mission->mission_ended = true;
        Mixer.stopSound();
        Mixer.stopSound(5);
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::SPEC_KEY_1))) {
        this->current_mission->gameflow_registers[0] = 1;
        this->current_mission->gameflow_registers[1] = 2;
        this->current_mission->mission_ended = true;
    }
    if (m_keyboard->isActionJustPressed(CreateAction(InputAction::SIM_START, SimActionOfst::SPEC_KEY_2))) {
        this->current_mission->gameflow_registers[0] = 0;
        this->current_mission->mission_ended = true;
    }
    float lx = m_keyboard->getActionValue(CreateAction(InputAction::SIM_START, SimActionOfst::CONTROLLER_STICK_LEFT_X)); 
    float ly = m_keyboard->getActionValue(CreateAction(InputAction::SIM_START, SimActionOfst::CONTROLLER_STICK_LEFT_Y)); 

    float rx = m_keyboard->getActionValue(CreateAction(InputAction::SIM_START, SimActionOfst::CONTROLLER_STICK_RIGHT_X));
    float ry = m_keyboard->getActionValue(CreateAction(InputAction::SIM_START, SimActionOfst::CONTROLLER_STICK_RIGHT_Y));

    if (fabs(lx) > 0.1f) {
        this->player_plane->control_stick_x = static_cast<int>(lx * (Screen->width/2.5f));
        this->mouse_control = false;
    }
    if (fabs(ly) > 0.1f) {
        this->player_plane->control_stick_y = static_cast<int>(-ly * (Screen->height/2.5f));
        this->mouse_control = false;
    }
    if (fabs(rx) > 0.1f) {
        this->pilote_lookat.x = static_cast<int>((rx * Screen->width) / 6);
        this->camera_mode = View::CONTROLLER_LOOK;
    }
    if (fabs(ry) > 0.1f) {
        this->pilote_lookat.y = static_cast<int>((ry * Screen->height) / 6);
        this->camera_mode = View::CONTROLLER_LOOK;
    }
    if (fabs(rx) < 0.1f && fabs(ry) < 0.1f && this->camera_mode == View::CONTROLLER_LOOK) {
        this->camera_mode = View::FRONT;
        this->pilote_lookat.x = 0;
        this->pilote_lookat.y = 0;
    }
    this->cockpit->mouse_control = this->mouse_control;
    this->checkGameSpecificKeyboard();
}
SCNavMap* SCStrike::createNavMap() {
    return new SCNavMap();
}
void SCStrike::findTarget() {
    const float target_range = 80000.0f;  // real confirmed max radar range
    const float target_range_sq = target_range * target_range;

    // 1. Collecter toutes les cibles valides dans la portée — user-
    // confirmed real behavior: T cycles through every trackable contact
    // (friend or foe), not just enemies, so this now scans the master
    // actors list rather than current_mission->enemies. Previously also
    // filtered on actor->team_id == player->team_id, a separate, unrelated
    // (and apparently unreliable — WC3Mission.cpp reads it from a raw,
    // never-confirmed "unknown_bytes[2]" field) classification from the
    // real friend/foe split already used to build current_mission-
    // >enemies/friendlies (see that split's own comment) — every enemy was
    // being filtered out here whenever the two team_id guesses happened to
    // coincide, which is why targeting was stuck on "none".
    // User-requested (2026-07 session): T should cycle through the actors
    // at the current nav point specifically, not just "everything in
    // range" — the current nav point's own area_id (waypoints.back()-style
    // "most recently pushed" entry, see the big comment on waypoints
    // below) is matched against each actor's own MISN_PART::area_id.
    // Falls back to the unfiltered in-range scan if that yields nothing
    // (e.g. no waypoint yet, or a nav point whose area has no actors of
    // its own — SIM missions and area_id==255/"no fixed area" actors),
    // so T still does something useful rather than going silent.
    short navPointAreaId = -1;
    bool haveNavPointArea = false;
    if (this->nav_point_id < this->current_mission->waypoints.size()) {
        SCMissionWaypoint *navWp = this->current_mission->waypoints[this->nav_point_id];
        if (navWp != nullptr && navWp->spot != nullptr) {
            navPointAreaId = navWp->spot->area_id;
            haveNavPointArea = true;
        }
    }

    auto collectCandidates = [&](bool filterByNavPointArea) {
        std::vector<int> result;
        for (int i = 0; i < (int)this->current_mission->actors.size(); i++) {
            auto actor = this->current_mission->actors[i];
            if (actor == nullptr || actor == this->current_mission->player) {
                continue;
            }
            if (!actor->is_active || actor->is_destroyed) {
                continue;
            }
            // User-confirmed (2026-07 session): cloaked contacts (e.g. the
            // Skipper Missile, SKIPMISS — cloaks every 3 seconds specifically
            // "to prevent missile lock") can't be targeted at all while
            // cloaked; only decloaked. SCMissionActors::UpdateCloak drives
            // actor->plane->cloaked once per SCMission::update() tick.
            if (actor->plane != nullptr && actor->plane->cloaked) {
                continue;
            }
            if (filterByNavPointArea && (short)actor->object->area_id != navPointAreaId) {
                continue;
            }
            float dx = actor->object->position.x - this->player_plane->x;
            float dy = actor->object->position.y - this->player_plane->y;
            float dz = actor->object->position.z - this->player_plane->z;
            float distSq = dx * dx + dy * dy + dz * dz;
            if (distSq <= target_range_sq) {
                result.push_back(i);
            }
        }
        return result;
    };

    std::vector<int> candidates = haveNavPointArea ? collectCandidates(true) : std::vector<int>();
    if (candidates.empty()) {
        candidates = collectCandidates(false);
    }

    if (candidates.empty()) {
        // Aucune cible disponible
        this->current_target = -1;
        this->target = nullptr;
        this->current_mission->player->target = nullptr;
        this->cockpit->target = nullptr;
        this->cockpit->target_manually_selected = false;
        printf("Target: none\n");
        return;
    }

    // 2. Trier les candidats par distance croissante
    std::sort(candidates.begin(), candidates.end(), [&](int a, int b) {
        auto ea = this->current_mission->actors[a];
        auto eb = this->current_mission->actors[b];
        float dxa = ea->object->position.x - this->player_plane->x;
        float dya = ea->object->position.y - this->player_plane->y;
        float dza = ea->object->position.z - this->player_plane->z;
        float dxb = eb->object->position.x - this->player_plane->x;
        float dyb = eb->object->position.y - this->player_plane->y;
        float dzb = eb->object->position.z - this->player_plane->z;
        return (dxa*dxa + dya*dya + dza*dza) < (dxb*dxb + dyb*dyb + dzb*dzb);
    });

    // 3. Trouver la position de la cible actuelle dans la liste
    int nextIndex = 0; // Par défaut: premier candidat (le plus proche)
    for (int i = 0; i < (int)candidates.size(); i++) {
        if (candidates[i] == this->current_target) {
            // Cycler vers le suivant
            nextIndex = (i + 1) % (int)candidates.size();
            break;
        }
    }

    // 4. Appliquer la nouvelle cible
    int selectedIndex = candidates[nextIndex];
    this->current_target = selectedIndex;
    this->target = this->current_mission->actors[selectedIndex];
    this->current_mission->player->target = this->target;
    this->cockpit->target = this->target->object;
    this->cockpit->target_manually_selected = true;
    printf("Target: %s\n", this->target->object->member_name.c_str());
}
// Real WC3-style auto-targeting: continuously targets whatever in-range
// enemy the player is oriented towards (nearest if several qualify), rather
// than findTarget()'s manual T-cycle. Skips entirely while
// cockpit->target_hard_locked is true (see SCCockpit.h) so a sticky L-lock
// keeps its target regardless of where the player looks; if that locked
// target dies or leaves a generous extended range, the lock is released
// here and auto-targeting resumes the same frame.
void SCStrike::updateAutoTarget() {
    if (this->cockpit->target_hard_locked) {
        bool stillValid = (this->target != nullptr && this->target->is_active && !this->target->is_destroyed &&
                            (this->target->plane == nullptr || !this->target->plane->cloaked));
        if (stillValid) {
            const float maxLockRange = 80000.0f * 1.5f;  // real confirmed max radar range
            float dx = this->target->object->position.x - this->player_plane->x;
            float dy = this->target->object->position.y - this->player_plane->y;
            float dz = this->target->object->position.z - this->player_plane->z;
            stillValid = (dx * dx + dy * dy + dz * dz) <= (maxLockRange * maxLockRange);
        }
        if (stillValid) {
            return;
        }
        this->cockpit->target_hard_locked = false;
        // Fall through so a target is re-acquired this same frame instead
        // of leaving current_target stale for one frame.
    }
    // Bug fix (2026-07 session, user-reported: pressing 'T' often didn't
    // seem to change the target): this function used to run completely
    // unconditionally whenever target_hard_locked was false, so it
    // overwrote findTarget()'s manual T-cycle pick back to "nearest thing
    // in the front cone" on essentially the very next frame — only
    // "sticking" when that happened to already be the same actor. Same
    // stillValid/fall-through shape as the hard-lock block above, just
    // without forcing the HUD's locked-square box style (target_hard_locked
    // stays false — the player selected this, didn't weapons-lock it).
    if (this->cockpit->target_manually_selected) {
        bool stillValid = (this->target != nullptr && this->target->is_active && !this->target->is_destroyed &&
                            (this->target->plane == nullptr || !this->target->plane->cloaked));
        if (stillValid) {
            const float maxLockRange = 80000.0f * 1.5f;  // real confirmed max radar range
            float dx = this->target->object->position.x - this->player_plane->x;
            float dy = this->target->object->position.y - this->player_plane->y;
            float dz = this->target->object->position.z - this->player_plane->z;
            stillValid = (dx * dx + dy * dy + dz * dz) <= (maxLockRange * maxLockRange);
        }
        if (stillValid) {
            return;
        }
        this->cockpit->target_manually_selected = false;
        // Fall through so a target is re-acquired this same frame instead
        // of leaving current_target stale for one frame.
    }

    const float target_range = 80000.0f;  // real confirmed max radar range
    const float target_range_sq = target_range * target_range;
    SCMissionActors *best = nullptr;
    // Only meaningful as an *enemy*-list index (matches findTarget()'s own
    // contract, since it cycles by comparing candidate indices straight
    // against this field) — stays -1 whenever the winning candidate is a
    // friendly, even though `target`/`cockpit->target` below do get set to
    // it. findTarget()'s manual T-cycle only ever considers enemies anyway.
    int bestEnemyIndex = -1;
    float bestDistSq = 0.0f;

    // Auto-targeting isn't just for weapons lock — the target box itself is
    // meant to highlight whatever ship (enemy or friendly) the player is
    // oriented towards, so both lists are real candidates here.
    auto considerCandidate = [&](SCMissionActors *actor, int enemyIndex) {
        if (actor == this->current_mission->player) {
            return;
        }
        if (!actor->is_active || actor->is_destroyed) {
            return;
        }
        // See findTarget()'s own matching comment — cloaked contacts can't
        // be targeted at all while cloaked.
        if (actor->plane != nullptr && actor->plane->cloaked) {
            return;
        }
        float dx = actor->object->position.x - this->player_plane->x;
        float dy = actor->object->position.y - this->player_plane->y;
        float dz = actor->object->position.z - this->player_plane->z;
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq > target_range_sq) {
            return;
        }
        if (SCMissionActors::ClassifyHitQuadrant(this->current_mission->player, actor->object->position) != HitQuadrant::Front) {
            return;
        }
        if (best == nullptr || distSq < bestDistSq) {
            best = actor;
            bestEnemyIndex = enemyIndex;
            bestDistSq = distSq;
        }
    };
    for (int i = 0; i < (int)this->current_mission->enemies.size(); i++) {
        considerCandidate(this->current_mission->enemies[i], i);
    }
    for (auto friendly : this->current_mission->friendlies) {
        considerCandidate(friendly, -1);
    }

    this->current_target = bestEnemyIndex;
    this->target = best;
    this->current_mission->player->target = best;
    this->cockpit->target = (best != nullptr) ? best->object : nullptr;
}
// See declaration comment (SCStrike.h).
//
// current_mission->waypoints is NOT a pre-populated route to fly through in
// order — it's a growing history log: mission scripting pushes a *new*
// entry every time the current objective changes (SCMissionActorsPlayer::
// takeOff/land/flyToWaypoint/flyToArea, in SCMissionActors.cpp), and old
// entries are left in place as history, not removed. So "the next nav
// point" is simply whichever entry was pushed most recently
// (waypoints.back()), not something reached by flying through prior ones.
//
// An earlier version of this function got that backwards — it treated
// nav_point_id as a cursor to proximity-advance forward through the list,
// which broke autopilot entirely: the player spawns right at the "take
// off" spot (the very first waypoint pushed), so on the first frame it
// would immediately count that as "reached" and advance nav_point_id past
// it — but since that's the *only* entry that exists yet, nav_point_id
// landed on an out-of-bounds index and stayed there, which makes
// autopilotCompute()'s own bounds check silently refuse to engage,
// independent of CanEngageAutopilot's enemy-proximity gate.
void SCStrike::updateNavPoint() {
    if (this->current_mission == nullptr) {
        return;
    }
    size_t count = this->current_mission->waypoints.size();
    if (count == 0) {
        return;
    }
    if (count != this->last_nav_point_count) {
        // A new current-objective waypoint was pushed since we last
        // checked (or this is the first one this mission) — auto-follow
        // it. Doesn't touch nav_point_id otherwise, so a manual pick made
        // via the nav-map screen (which writes nav_point_id directly)
        // sticks until the next real objective change.
        this->nav_point_id = (uint8_t)((count - 1 > 255) ? 255 : count - 1);
        this->last_nav_point_count = count;
    }
    // Proximity auto-advance (user-requested, 2026-07 session): once the
    // player gets within kNavPointReachedRadius of the currently-selected
    // nav point, move on to the next real (non-takeoff/landing) entry
    // already present in the waypoint history. Deliberately bounded, not
    // wrap-around, and only ever steps *forward* from nav_point_id — see
    // this function's own doc comment above for the exact bug an earlier,
    // unbounded version of this idea caused (advanced past the single
    // "take off" entry that exists at mission start into an out-of-bounds
    // index, permanently disabling autopilot). With this forward-only,
    // bounds-checked search, that same moment is naturally a no-op: with
    // only 1 entry, there's nothing at nav_point_id+1 to search, so the
    // loop below never even runs.
    if (this->player_plane != nullptr && this->nav_point_id < count) {
        SCMissionWaypoint *current = this->current_mission->waypoints[this->nav_point_id];
        if (current != nullptr && current->spot != nullptr) {
            const float kNavPointReachedRadius = 1000.0f;
            Vector3D navPos = current->spot->position;
            Vector3D playerPos = {this->player_plane->x, this->player_plane->y, this->player_plane->z};
            if ((navPos - playerPos).Length() < kNavPointReachedRadius) {
                for (size_t idx = (size_t)this->nav_point_id + 1; idx < count; idx++) {
                    if (!SCMissionWaypoint::IsTakeoffOrLanding(this->current_mission->waypoints[idx])) {
                        this->nav_point_id = (uint8_t)((idx > 255) ? 255 : idx);
                        break;
                    }
                }
            }
        }
    }
}
// See declaration comment (SCStrike.h). "Immediate area" — no real data
// source for this either, picked smaller than updateAutoTarget's 80000-unit
// max radar range (autopilot should refuse well before a hostile is merely
// on radar, but there's no reverse-engineered real value to match against).
// Tunable.
bool SCStrike::CanEngageAutopilot() {
    if (this->current_mission == nullptr || this->player_plane == nullptr) {
        return true;
    }
    const float kAutopilotBlockRange = 20000.0f;
    const float kAutopilotBlockRangeSq = kAutopilotBlockRange * kAutopilotBlockRange;
    for (auto enemy : this->current_mission->enemies) {
        if (enemy == nullptr || !enemy->is_active || enemy->is_destroyed || enemy->object == nullptr) {
            continue;
        }
        std::string name = enemy->object->member_name;
        std::transform(name.begin(), name.end(), name.begin(), ::toupper);
        if (SCMissionActors::IsCapitalShipName(name)) {
            continue;
        }
        float dx = enemy->object->position.x - this->player_plane->x;
        float dy = enemy->object->position.y - this->player_plane->y;
        float dz = enemy->object->position.z - this->player_plane->z;
        if (dx * dx + dy * dy + dz * dz <= kAutopilotBlockRangeSq) {
            return false;
        }
    }
    return true;
}
/**
 * SCStrike::Init
 *
 * Initialize the game state with default values and a starting mission.
 *
 * This function initializes the game state by setting the mouse control
 * flag to false, setting the mission to "TEMPLATE.IFF", initializing the
 * cockpit object, and setting the pilot's lookat position to the origin.
 */
void SCStrike::init(void) {
    this->mouse_control = false;
    Game->direct_mouse_control = true;
    this->pilote_lookat = {0, 0};
    this->registerSimulatorInputs();
    Config &cfg = Config::getInstance();
    this->world_lod = cfg.getInt("Game", "world_detail", 0);
    this->virtual_mouse_cockpit_buffer = new FrameBuffer(320, 200);
}

RSEntity * SCStrike::loadWeapon(std::string name) {
    std::string tmpname = Assets.object_root_path + name + ".IFF";
    RSEntity *objct = new RSEntity();
    TreEntry *entry = Assets.GetEntryByName(tmpname);
    if (entry != nullptr) {
        objct->InitFromRAM(entry->data, entry->size, tmpname);
        return objct;
    }
    return nullptr;
}

/**
 * SetMission
 *
 * Sets the current mission to the one specified in the missionName argument.
 *
 * This function initializes the game state by setting the mission to the one
 * specified in the missionName argument, clearing the list of AI planes,
 * initializing the cockpit object, and setting the pilot's lookat position to
 * the origin. It also loads the necessary data from the mission file, such as
 * the player's coordinates and the objects in the mission.
 *
 * @param[in] missionName The name of the mission file to load.
 */
void SCStrike::setMission(char const *missionName) {
    if (Mixer.isSoundPlaying(5)) {
        Mixer.stopSound(5);
    }
    Mixer.stopMusic();
    
    if (this->current_mission != nullptr) {
        this->current_mission->cleanup();
        delete this->current_mission;
        this->current_mission = nullptr;
    } 
    this->miss_file_name = missionName;
    this->current_mission = new SCMission(missionName, &object_cache);
    ai_planes.clear();
    ai_planes.shrink_to_fit();
    
    MISN_PART *playerCoord = this->current_mission->player->object;
    this->area = this->current_mission->area;
    new_position.x = playerCoord->position.x;
    new_position.z = playerCoord->position.z;
    new_position.y = playerCoord->position.y;

    camera = Renderer.getCamera();
    camera->SetPosition(&new_position);
    this->current_target = 0;
    this->target = this->current_mission->enemies[this->current_target];
    this->nav_point_id = 0;
    this->last_nav_point_count = 0;
    this->player_plane = this->current_mission->player->plane;
    this->player_plane->yaw = (360 - playerCoord->azymuth) * 10.0f;
    this->player_plane->object = playerCoord;
    // area is null in WC3 space missions (see WC3Mission::is_space_mission)
    // — nothing to taxi/take off from, so just skip the roll simulation.
    float ground = (this->area != nullptr) ? this->area->getY(new_position.x, new_position.z) : new_position.y;
    if (fabs(ground - new_position.y) > 10.0f) {
        this->player_plane->SetThrottle(100);
        this->player_plane->SetWheel();
        this->player_plane->vz = -20;
        this->player_plane->Simulate();
    }
    if (GameState.weapon_load_out.size()>1) {
        this->player_plane->object->entity->weaps.clear();
        std::unordered_map<weapon_type_shp_id, std::string> weapon_names = {
            {AIM9J, "SWINDERJ"},
            {AIM9M, "SWINDERM"},
            {AIM120, "AMRAAM"},
            {AGM65D, "AGM-65D"},
            {DURANDAL, "DURANDAL"},
            {MK20, "MK20"},
            {MK82, "MK82"},
            {GBU15, "GBU-15G"},
            {LAU3, "POD"}
        };
        RSEntity::WEAPS *weap = new RSEntity::WEAPS();
        weap->name = "20MM";
        weap->nb_weap = 1000;
        weap->objct = loadWeapon(weap->name);
        this->player_plane->object->entity->weaps.push_back(weap);
        std::unordered_map<weapon_type_shp_id, bool> loaded;
        for (int i=1; i<5; i++) {
            if (loaded.find(weapon_type_shp_id(GameState.weapon_load_out[i])) != loaded.end()) {
                continue;
            }
            RSEntity::WEAPS *weap = new RSEntity::WEAPS();
            weap->name = weapon_names[weapon_type_shp_id(GameState.weapon_load_out[i])];
            weap->nb_weap = GameState.weapon_load_out[GameState.weapon_load_out[i]];
            weap->objct = loadWeapon(weap->name);
            this->player_plane->object->entity->weaps.push_back(weap);
            loaded[weapon_type_shp_id(GameState.weapon_load_out[i])] = true;
        }
    }
    
    this->player_plane->InitLoadout();
    this->player_prof = this->current_mission->player->profile;
    this->cockpit = new SCCockpit();
    this->cockpit->player_plane = this->player_plane;
    this->cockpit->init();
    this->cockpit->current_mission = this->current_mission;
    this->cockpit->nav_point_id = &this->nav_point_id;
    Mixer.stopMusic();
    Mixer.switchBank(2);
    Mixer.playMusic(this->current_mission->mission->mission_data.tune+1);
}
void SCStrike::updateMissionMusic() {
    if (this->current_mission->mission_over && this->current_mission->mission_won) {
        this->Mixer.playMusic(13); // play victory music
    } else if (this->current_mission->mission_over && !this->current_mission->mission_won) {
        this->Mixer.playMusic(12); // play victory music
    } else if (this->current_mission->in_combat) {
        this->Mixer.playMusic(5);
    } else {
        this->Mixer.playMusic(this->current_mission->mission->mission_data.tune+1);
    }
}
bool SCStrike::findCarrierActor(SCMissionActors *&outActor) {
    if (this->current_mission == nullptr) {
        return false;
    }
    for (auto actor : this->current_mission->actors) {
        if (actor->object == nullptr || actor->object->entity == nullptr) {
            continue;
        }
        if (actor->object->entity->flight_deck_entity == nullptr) {
            continue;
        }
        outActor = actor;
        return true;
    }
    return false;
}
bool SCStrike::getDockedCarrierYawTenths(float &yawTenths) {
    if (this->current_mission == nullptr || this->player_plane == nullptr) {
        return false;
    }
    SCMissionActors *carrier = nullptr;
    if (!this->findCarrierActor(carrier)) {
        return false;
    }
    Vector3D d = {
        carrier->object->position.x - this->player_plane->x,
        carrier->object->position.y - this->player_plane->y,
        carrier->object->position.z - this->player_plane->z
    };
    if (d.Length() < HANGAR_INTERIOR_RADIUS) {
        yawTenths = (360.0f - static_cast<float>(carrier->object->azymuth)) * 10.0f;
        return true;
    }
    return false;
}
void SCStrike::setCameraFront() {
    Vector3D pos = {this->new_position.x, this->new_position.y, this->new_position.z};
    camera->SetPosition(&pos);
    camera->resetRotate();
    camera->rotate(
        -tenthOfDegreeToRad(this->player_plane->elevationf),
        -tenthOfDegreeToRad(this->player_plane->azimuthf),
        -tenthOfDegreeToRad(this->player_plane->twist)
    );
    static int align_debug_count = 0;
    if (align_debug_count < 10) {
        align_debug_count++;
        printf("DEBUG ALIGN(FRONT): player yaw=%.1f azimuthf=%.1f pitch=%.1f elevationf=%.1f roll=%.1f twist=%.1f player_azymuth_raw=%u player_pos=(%.1f,%.1f,%.1f)\n",
               this->player_plane->yaw, this->player_plane->azimuthf,
               this->player_plane->pitch, this->player_plane->elevationf,
               this->player_plane->roll, this->player_plane->twist,
               (unsigned)this->player_plane->object->azymuth,
               this->player_plane->x, this->player_plane->y, this->player_plane->z);
        for (auto a : this->current_mission->actors) {
            if (a->object->entity != nullptr && a->object->entity->flight_deck_entity != nullptr) {
                printf("DEBUG ALIGN(FRONT): carrier '%s' azymuth_raw=%u pitch=%d roll=%d pos=(%.1f,%.1f,%.1f)\n",
                       a->actor_name.c_str(), (unsigned)a->object->azymuth,
                       (int)a->object->pitch, (int)a->object->roll,
                       a->object->position.x, a->object->position.y, a->object->position.z);
            }
        }
    }
}
void SCStrike::setCameraFollow(SCPlane *plane) {
    const float distanceBehind = -65.0f;
    float r_azim = tenthOfDegreeToRad(plane->azimuthf);
    float r_elev = tenthOfDegreeToRad(plane->elevationf-100.0f);
    float r_twist = tenthOfDegreeToRad(plane->twist);
    Vector3D camPos;
    camPos.x = plane->x - distanceBehind * cos(r_elev) * sin(r_azim);
    camPos.y = plane->y + distanceBehind * sin(r_elev);
    camPos.z = plane->z - distanceBehind * cos(r_elev) * cos(r_azim);
    camera->SetPosition(&camPos);

    Vector3D camLookAt = {plane->x, plane->y, plane->z};
    float cosT = cos(r_twist), sinT = sin(r_twist);
    float cosE = cos(r_elev), sinE = sin(r_elev);
    float cosA = cos(r_azim), sinA = sin(r_azim);

    // Compute the up vector as the second column of the composite rotation matrix
    // R = Ry(azim) * Rx(elev) * Rz(twist)
    Vector3D up;
    up.x = -cosA * sinT + sinA * sinE * cosT;
    up.y = cosE * cosT;
    up.z = sinA * sinT + cosA * sinE * cosT;
    if (follow_dynamic) {
        camera->lookAt(&camLookAt, &up);
    } else {
        camera->lookAt(&camLookAt);
    }
}
void SCStrike::setCameraBelow(SCPlane *plane) {
    float r_azim = tenthOfDegreeToRad(plane->azimuthf);
    float r_elev = tenthOfDegreeToRad(plane->elevationf);
    float r_twist = tenthOfDegreeToRad(plane->twist);
    float cosT = cos(r_twist), sinT = sin(r_twist);
    float cosE = cos(r_elev), sinE = sin(r_elev);
    float cosA = cos(r_azim), sinA = sin(r_azim);
    Vector3D up;
    up.x = -cosA * sinT + sinA * sinE * cosT;
    up.y = cosE * cosT;
    up.z = sinA * sinT + cosA * sinE * cosT;

    const float distanceBelow = 60.0f;
    Vector3D camPos = {
        plane->x - up.x * distanceBelow,
        plane->y - up.y * distanceBelow,
        plane->z - up.z * distanceBelow
    };
    camera->SetPosition(&camPos);
    Vector3D camLookAt = {plane->x, plane->y, plane->z};
    camera->lookAt(&camLookAt, &up);
}
void SCStrike::setCameraRLR() {
    camera->SetPosition(&this->new_position);
    camera->resetRotate();
    camera->rotate(0.0f, this->pilote_lookat.x * ((float)M_PI / 180.0f), 0.0f);
    camera->rotate(
        -tenthOfDegreeToRad(this->player_plane->elevationf),
        -tenthOfDegreeToRad(this->player_plane->azimuthf),
        -tenthOfDegreeToRad(this->player_plane->twist)
    );
}
void SCStrike::setCameraLookat(Vector3D obj_pos) {
    Vector3D pos = {
        this->player_plane->x,
        this->player_plane->y,
        this->player_plane->z
    };
    Vector3D dir = {
        obj_pos.x - pos.x,
        obj_pos.y - pos.y,
        obj_pos.z - pos.z
    };
    Vector3D lookAt;
    Vector3D camPos;

    float len = dir.Length();
    if (len > 20000.0f) {
        Vector3D forward = this->player_plane->forward;
        forward.Normalize();
        lookAt = {
            pos.x + forward.x * 10.0f,
            pos.y + forward.y * 10.0f,
            pos.z + forward.z * 10.0f
        };
        camPos = pos;
    } else {
        dir.Normalize();

        // Calcul de la taille réelle de la cible via sa BoundingBox
        float targetSize = 30.0f; // valeur par défaut
        if (this->target != nullptr && this->target->object != nullptr && this->target->object->entity != nullptr) {
            BoudingBox *bb = this->target->object->entity->GetBoudingBpx();
            if (bb != nullptr) {
                float sizeX = bb->max.x - bb->min.x;
                float sizeY = bb->max.y - bb->min.y;
                float sizeZ = bb->max.z - bb->min.z;
                // On prend la diagonale de la bounding box comme taille de référence
                targetSize = sqrt(sizeX*sizeX + sizeY*sizeY + sizeZ*sizeZ);
            }
        }

        // On veut que la cible occupe toujours TARGET_ANGULAR_SIZE degrés à l'écran
        // d = targetSize / (2 * tan(angle/2))
        const float TARGET_ANGULAR_SIZE = 15.0f; // degrés
        float camDist = targetSize / (2.0f * tan(TARGET_ANGULAR_SIZE * ((float)M_PI / 180.0f) / 2.0f));

        camDist = (std::max)(camDist, 10.0f); // pas trop près
        camDist = (std::min)(camDist, len);   // pas au-delà de la cible

        camPos = {
            obj_pos.x - dir.x * camDist,
            obj_pos.y - dir.y * camDist,
            obj_pos.z - dir.z * camDist
        };
        lookAt = {
            obj_pos.x,
            obj_pos.y + 10.0f,
            obj_pos.z
        };
    }
    Vector3D up;

    float r_twist = tenthOfDegreeToRad(this->player_plane->twist);
    float r_elev  = tenthOfDegreeToRad(this->player_plane->elevationf);
    float r_azim  = tenthOfDegreeToRad(this->player_plane->azimuthf);
    float cosT = cos(r_twist), sinT = sin(r_twist);
    float cosE = cos(r_elev), sinE = sin(r_elev);
    float cosA = cos(r_azim), sinA = sin(r_azim);
    up.x = -cosA * sinT + sinA * sinE * cosT;
    up.y = cosE * cosT;
    up.z = sinA * sinT + cosA * sinE * cosT;
    camera->SetPosition(&camPos);
    camera->lookAt(&lookAt ,&up);
}
/**
 * @brief Executes a single frame of the game simulation.
 *
 * This function performs the main loop operations for each frame of the game
 * simulation. It handles keyboard input check, updates the states of the player
 * plane and AI planes, adjusts the camera view based on the current mode, and
 * renders the game world and cockpit.
 *
 * Key operations include:
 * - Checking and processing keyboard inputs.
 * - Simulating the player and AI planes' movements and autopilot systems.
 * - Updating positions and orientations of planes and mission objects.
 * - Configuring the camera based on the selected view mode.
 * - Rendering the world, mission objects, and cockpit interface.
 * - Managing the display of cockpit elements like communication and weapons.
 */
void SCStrike::runFrame(void) {
    Mixer.setVolume(5,5);
    this->checkKeyboard();
    Renderer.setLight(&this->light);
    if (!this->pause_simu && this->camera_mode!=View::AUTO_PILOT) {
        // While docked, force the plane's actual simulated heading (not
        // just the camera's rendered one — see getDockedCarrierYawTenths)
        // to match the carrier's, otherwise the visual forward direction
        // (camera, now aligned to the carrier) and the physics forward
        // direction (ptw/thrust, still built from the player's own
        // unreliable spawn yaw) disagree and the ship appears to drift
        // sideways relative to what's on screen.
        float dockedYawTenths;
        bool isDocked = this->getDockedCarrierYawTenths(dockedYawTenths);
        if (isDocked) {
            this->player_plane->yaw = dockedYawTenths;
        }
        // Grey motion-dust particles (see SCRenderer::renderSpaceDust) —
        // shouldn't appear while still inside the TCS Victory's hangar bay,
        // only once the player has actually flown out into open space.
        // show_starfield is already only true for WC3 space missions (set
        // in WC3Strike::setMission), so gating on it here keeps this base,
        // game-agnostic SCStrike::runFrame() from needing to know anything
        // WC3-specific.
        // Disabled for now (user request, 2026-07 session): the respawn
        // logic in SCRenderer::renderSpaceDust() makes particles look like
        // they're randomly jumping around rather than a static field the
        // player flies through, and the first attempt at fixing that
        // (biasing respawn direction using a per-frame camera-position
        // delta) broke ship movement for a reason that wasn't diagnosed —
        // see the "SCRenderer camera/movement coupling" memory. Revisit
        // with a different approach; the plain
        // `Renderer.show_starfield && !isDocked` gate below is what this
        // should go back to once that's sorted.
        Renderer.show_dust = false;
        this->player_plane->Simulate();
        // current_mission->update()'s own per-actor loop keeps every AI
        // actor's MISN_PART orientation (object->azymuth/pitch/roll) synced
        // from its live SCPlane each tick, but explicitly skips the player
        // (physics for the player runs separately, above). Nothing else
        // ever wrote these back for the player's own object, so
        // player->object->azymuth stayed frozen at its mission-start spawn
        // heading no matter how the player actually turned — silently
        // breaking anything that reads the player's MISN_PART facing (e.g.
        // updateAutoTarget's ClassifyHitQuadrant call, right below, which
        // was therefore checking the player's aspect against a heading that
        // never changed). Mirrors the exact conversion SCMission.cpp's AI
        // loop uses for its own actors.
        if (this->player_plane->object != nullptr) {
            this->player_plane->object->azymuth = 360 - (uint16_t)(this->player_plane->azimuthf / 10.0f);
            this->player_plane->object->pitch = (uint16_t)(this->player_plane->elevationf / 10.0f);
            this->player_plane->object->roll = (uint16_t)(this->player_plane->twist / 10.0f);
        }
        this->current_mission->update();
        this->updateAutoTarget();
        this->updateNavPoint();
        // 3-state AUTO light (user-confirmed real WC3 behavior, 2026-07
        // session): on before the player has left the hangar bay (ready to
        // take off), on again once landing clearance is granted (ready to
        // land), and reflecting the normal enemy-proximity gate the rest of
        // the mission.
        if (!this->current_mission->has_left_carrier_bay || this->current_mission->landing_clearance_granted) {
            this->cockpit->autopilot_available = true;
        } else {
            this->cockpit->autopilot_available = this->CanEngageAutopilot();
        }
        this->updateMissionMusic();
        if (this->current_mission->mission_ended) {
            GameState.missions_flags.clear();
            GameState.missions_flags.shrink_to_fit();
            // @TODO Rework this part when we will be dealing with the mission funds bonus
            for (auto flags: this->current_mission->gameflow_registers) {
                GameState.missions_flags.push_back(flags.second);
            }
            //GameState.cash = GameState.proj_cash + this->current_mission->gameflow_registers[1]*1000;
            GameState.proj_cash = GameState.proj_cash + this->current_mission->gameflow_registers[1]*1000 - GameState.over_head - GameState.weapons_costs - GameState.f16_replacements ;
            GameState.weapons_costs = 0;
            GameState.mission_flyed_success[GameState.mission_flyed] = this->current_mission->gameflow_registers[0]>0;
            //GameState.kill_board[PilotsId::PLAYER][KillBoardType::AIR_KILL] += this->current_mission->player->plane_down;
            //GameState.kill_board[PilotsId::PLAYER][KillBoardType::GROUND_KILL] += this->current_mission->player->ground_down;
            GameState.air_kills += this->current_mission->player->plane_down;
            GameState.ground_kills += this->current_mission->player->ground_down;
            GameState.kill_board[PilotsId::PLAYER][KillBoardType::AIR_KILL] =GameState.air_kills;
            GameState.kill_board[PilotsId::PLAYER][KillBoardType::GROUND_KILL] = GameState.ground_kills;
            
            for (auto team: this->current_mission->friendlies) {
                if (team->is_active) {
                    if (team->plane != nullptr && team->actor_name != "PLAYER") {
                        GameState.kill_board[pilot_profile[team->actor_name]][KillBoardType::AIR_KILL] += team->plane_down;
                        GameState.kill_board[pilot_profile[team->actor_name]][KillBoardType::GROUND_KILL] += team->ground_down;
                    }
                }
            }
            Renderer.clear();
            Screen->refresh();
            Renderer.clear();
            cleanupVirtualCockpitTextures();
            this->onMissionEnded();
            Game->stopTopActivity();
            return;
        }
    }
    if (this->zoom_cockpit) {
        Renderer.camera.fovy = 30.0f;
        Renderer.camera.update();
    } else {
        // Real per-mode field of view from WORLD.IFF's WRLD>CAMR sub-
        // chunks (RSWorld::camCockpit/camTarget/camChase/camTrack/
        // camWeapon — user-requested, 2026-07 session: use the real IFF
        // camera data instead of guessed constants; the cockpit-forward
        // 30.0f here specifically supersedes an earlier 25.0f derived
        // from screenshot pixel measurements once this real value came to
        // light — see camCockpit's own comment for that reconciliation).
        // 30.0f is also the fallback for missions with no WRLD loaded, or
        // for camera_mode values with no confident real-chunk match yet
        // (EYE_ON_TARGET/OBJECT/AUTO_PILOT — AUTO_PILOT's real camera data
        // is WorldCameraAutoSequence's own richer, still-unconsumed shape,
        // not this simple fixed-FOV one).
        float fovy = 30.0f;
        RSWorld *world = (this->current_mission != nullptr) ? this->current_mission->world : nullptr;
        if (world != nullptr) {
            switch (this->camera_mode) {
            case View::FRONT:
            case View::REAL:
            case View::LEFT:
            case View::RIGHT:
            case View::REAR:
            case View::CONTROLLER_LOOK:
                if (world->camCockpit.fovDegrees > 0) fovy = (float)world->camCockpit.fovDegrees;
                break;
            case View::TARGET:
                if (world->camTarget.fovDegrees > 0) fovy = (float)world->camTarget.fovDegrees;
                break;
            case View::FOLLOW:
                if (world->camChase.fovDegrees > 0) fovy = (float)world->camChase.fovDegrees;
                break;
            case View::TRACK:
                if (world->camTrack.fovDegrees > 0) fovy = (float)world->camTrack.fovDegrees;
                break;
            case View::MISSILE_CAM:
                if (world->camWeapon.fovDegrees > 0) fovy = (float)world->camWeapon.fovDegrees;
                break;
            default:
                break;
            }
        }
        Renderer.camera.fovy = fovy;
        Renderer.camera.update();
    }
    this->player_plane->getPosition(&new_position);
    if (this->player_plane->object != nullptr) {
        this->player_plane->object->position.x = new_position.x;
        this->player_plane->object->position.z = new_position.z;
        this->player_plane->object->position.y = new_position.y;
    }
    this->cockpit->Update();

    Vector3D target_position = {0,0,0};
    if (this->target != nullptr) {
        if (this->target->object != nullptr) {
            target_position = {
                this->target->object->position.x, 
                this->target->object->position.y, 
                this->target->object->position.z
            };
        }
    }
    this->setCameraLookat(target_position);
    Renderer.initRenderToTexture();
    Renderer.verticalOffset = 0.0f;
    Renderer.initRenderCameraView();
    Renderer.renderWorldToTexture(area);
    
    if (this->target != nullptr) {
        if (this->target->plane != nullptr) {
            this->target->plane->Render();    
        } else {
            Renderer.drawModel(this->target->object->entity, target_position, {0.0f, 0.0f, 0.0f});
        }
    }
    
    Renderer.getRenderToTexture();
    Renderer.verticalOffset = -0.45f;
    Renderer.initRenderCameraView();
    switch (this->camera_mode) {
    case View::AUTO_PILOT: {
        if (this->autopilot_sequence != AutopilotSequence::NONE) {
            this->updateAutopilotSequence();
            break;
        }
        if (this->autopilot_timeout > -AUTOPILOTE_TIMEOUT) {
            this->autopilot_timeout -= AUTOPILOTE_SPEED;
            // Real user-observed shot (2026-07 session): an angled view of
            // the (already-teleported, stationary) ship as the camera
            // itself sweeps past it, continuously rotating via lookAt to
            // track it — the +5/+5 lateral offset this used to have was too
            // small to read as "angled" at this scale (the camera's Z sweep
            // alone spans autopilot_timeout's full 1400-unit range), so the
            // lateral offset is widened to a proper angled-flyby distance.
            //
            // Usually frames the player and a single wingman (real user-
            // observed default), not just the player alone — in escort
            // missions the "wingman" slot here is whatever ship is actually
            // being escorted (mission-specific: usually a transport, but
            // sometimes the Behemoth or the TCS Victory). There's no
            // dedicated "escort target" field anywhere in the mission data
            // model, so this reuses the same friendlies-list scan
            // autopilotCompute() already does to teleport wingmen into
            // formation just above — whichever active non-player friendly
            // plane comes up first there is framed here too. Falls back to
            // the single-ship framing if no such plane exists.
            constexpr float kAngledOffset = 60.0f;
            SCPlane *wingman = nullptr;
            for (auto team : this->current_mission->friendlies) {
                if (team->is_active && team->plane != nullptr && team->plane != this->player_plane) {
                    wingman = team->plane;
                    break;
                }
            }
            Vector3D frameCenter = this->new_position;
            if (wingman != nullptr) {
                frameCenter = {
                    (this->new_position.x + wingman->x) * 0.5f,
                    (this->new_position.y + wingman->y) * 0.5f,
                    (this->new_position.z + wingman->z) * 0.5f
                };
            }
            Vector3D pos = {frameCenter.x + kAngledOffset, frameCenter.y + kAngledOffset * 0.5f,
                frameCenter.z - (autopilot_timeout)};
            camera->SetPosition(&pos);
            camera->lookAt(&frameCenter);
        } else {
            this->player_plane->ptw.Identity();
            this->player_plane->ptw.translateM(this->player_plane->x, this->player_plane->y, this->player_plane->z);
            this->player_plane->ptw.rotateM(degreeToRad(this->autopilot_target_azimuth), 0, 1, 0);
            this->player_plane->yaw = this->autopilot_target_azimuth * 10.0f;
            this->player_plane->Simulate();
            this->camera_mode = View::FRONT;
            for (auto team: this->current_mission->friendlies) {
                if (team->is_active) {
                    if (team->plane != nullptr && team->plane != this->player_plane) {                       
                        team->plane->yaw=this->autopilot_target_azimuth * 10.0f;
                    }
                }
            }
        }
    } break;
    case View::FRONT: {
        this->setCameraFront();
         // Apply a vertical offset to the projection matrix
    } break;
    case View::FOLLOW: {
        this->setCameraFollow(this->player_plane);
    } break;
    case View::RIGHT:
    case View::LEFT:
    case View::REAR:
        this->setCameraRLR();
        break;
    case View::TARGET: {
        if (this->target == nullptr) {
            this->camera_mode = View::FRONT;
            break;
        }
        if (this->target->object == nullptr) {
            this->camera_mode = View::FRONT;
            break;
        }
        setCameraLookat(this->target->object->position);
    }
    break;
    case View::OBJECT: {
        setCameraLookat(this->current_mission->actors[this->current_object_to_view]->object->position);
    }
    break;
    case View::MISSILE_CAM: {
        if (this->player_plane->weaps_object.size() == 0) {
            this->camera_mode = View::FRONT;
            break;
        }
        SCSimulatedObject *missile = this->player_plane->weaps_object.back();
        if (missile == nullptr) {
            this->camera_mode = View::FRONT;
            break;
        }
        Vector3D missil_pos{0,0,0};
        missile->GetPosition(&missil_pos);
        Vector3D mpos = {
            missil_pos.x+this->camera_pos.x,
            missil_pos.y+this->camera_pos.y,
            missil_pos.z+this->camera_pos.z
        };
        const float distanceBehind = -60.0f;
        float r_azim = degreeToRad(missile->azimuthf);
        float r_elev = degreeToRad(missile->elevationf);
        float r_twist = 0.0f;
        Vector3D camPos;
        camPos.x = missil_pos.x - distanceBehind * cos(r_elev) * sin(r_azim);
        camPos.y = missil_pos.y + distanceBehind * sin(r_elev);
        camPos.z = missil_pos.z - distanceBehind * cos(r_elev) * cos(r_azim);
        camera->SetPosition(&camPos);
        camera->lookAt(&missil_pos);
    }
    break;
    case View::EYE_ON_TARGET: {
        if (this->target == nullptr) {
            this->camera_mode = View::FRONT;
            break;
        }
        Vector3D pos = {this->new_position.x, this->new_position.y + this->eye_y, this->new_position.z};
        

        Vector3D camPos;
        camPos.x = this->new_position.x;
        camPos.y = this->new_position.y;
        camPos.z = this->new_position.z;
        
        // Compute the up vector as the second column of the composite rotation matrix
        // R = Ry(azim) * Rx(elev) * Rz(twist)
        Vector3D up;

        float r_twist = tenthOfDegreeToRad(this->player_plane->twist);
        float r_elev  = tenthOfDegreeToRad(this->player_plane->elevationf);
        float r_azim  = tenthOfDegreeToRad(this->player_plane->azimuthf);
        float cosT = cos(r_twist), sinT = sin(r_twist);
        float cosE = cos(r_elev), sinE = sin(r_elev);
        float cosA = cos(r_azim), sinA = sin(r_azim);
        up.x = -cosA * sinT + sinA * sinE * cosT;
        up.y = cosE * cosT;
        up.z = sinA * sinT + cosA * sinE * cosT;
        
        Vector3D targetPos = {target->object->position.x, target->object->position.y,target->object->position.z };
        camera->SetPosition(&camPos);
        camera->lookAt(&targetPos, &up);
    } break;
    // Real WC3 "Track Camera" (F10). World-fixed anchor set once on entry
    // (checkKeyboard's VIEW_TRACK handler) — continuously re-aims at the
    // player every frame as they fly past, unlike View::FOLLOW (which
    // moves with the player) or View::TARGET (which stays at the player's
    // own position looking outward).
    case View::TRACK: {
        Vector3D lookAtPos = {this->player_plane->x, this->player_plane->y, this->player_plane->z};
        camera->SetPosition(&this->track_camera_anchor);
        camera->lookAt(&lookAtPos);
    } break;
    case View::REAL:
    case View::CONTROLLER_LOOK:
    default: {
        Vector3D pos = {this->new_position.x, this->new_position.y + this->eye_y, this->new_position.z};
        camera->SetPosition(&pos);
        camera->resetRotate();

        camera->rotate(-this->pilote_lookat.y * ((float)M_PI / 180.0f), 0.0f, 0.0f);
        camera->rotate(0.0f, this->pilote_lookat.x * ((float)M_PI / 180.0f), 0.0f);

        camera->rotate(
            -tenthOfDegreeToRad(this->player_plane->elevationf),
            -tenthOfDegreeToRad(this->player_plane->azimuthf),
            -tenthOfDegreeToRad(this->player_plane->twist)
        );
        static int align_debug_count = 0;
        if (align_debug_count < 10) {
            align_debug_count++;
            printf("DEBUG ALIGN: player yaw=%.1f azimuthf=%.1f pitch=%.1f elevationf=%.1f roll=%.1f twist=%.1f player_azymuth_raw=%u player_pos=(%.1f,%.1f,%.1f)\n",
                   this->player_plane->yaw, this->player_plane->azimuthf,
                   this->player_plane->pitch, this->player_plane->elevationf,
                   this->player_plane->roll, this->player_plane->twist,
                   (unsigned)this->player_plane->object->azymuth,
                   this->player_plane->x, this->player_plane->y, this->player_plane->z);
            for (auto a : this->current_mission->actors) {
                if (a->object->entity != nullptr && a->object->entity->flight_deck_entity != nullptr) {
                    printf("DEBUG ALIGN: carrier '%s' azymuth_raw=%u pitch=%d roll=%d pos=(%.1f,%.1f,%.1f)\n",
                           a->actor_name.c_str(), (unsigned)a->object->azymuth,
                           (int)a->object->pitch, (int)a->object->roll,
                           a->object->position.x, a->object->position.y, a->object->position.z);
                }
            }
        }
    } break;
    }

    auto renderScene = [&](int32_t viewportW, int32_t viewportH, float projVerticalOffset, bool forceVirtualCockpit) {
        Renderer.bindCameraProjectionAndViewViewport(viewportW, viewportH, projVerticalOffset);
        Renderer.renderWorldSolid(area, this->world_lod, 400);

        if (this->show_bbox) {
            for (auto rrarea: this->current_mission->mission->mission_data.areas) {
                Renderer.renderLineCube(rrarea->position, rrarea->AreaWidth);
            }
        }

        for (auto actor: this->current_mission->actors) {
            if (actor->is_hidden) {
                continue;
            }
            if (actor->plane != nullptr) {
                // The player's own ship is normally excluded here (you're
                // inside its cockpit, rendering the model would just block
                // the view) — but in an external camera mode like FOLLOW
                // (chase cam, F2/VIEW_BEHIND) it should still be drawn,
                // otherwise there's nothing to see (confirmed by live
                // testing: chase cam showed an empty view).
                bool isPlayerShip = (actor->plane == this->player_plane);
                bool showPlayerShipExternally = isPlayerShip && this->camera_mode == View::FOLLOW;
                if (!isPlayerShip || showPlayerShipExternally) {
                    Vector3D distance = {
                        actor->plane->x - this->player_plane->x,
                        actor->plane->y - this->player_plane->y,
                        actor->plane->z - this->player_plane->z
                    };
                    if (distance.Length() > RENDER_DISTANCE) {
                        continue; // Skip rendering if the plane is too far away
                    }
                    if (this->show_bbox) {
                        actor->plane->renderPlaneLined();
                        BoudingBox *bb = actor->plane->object->entity->GetBoudingBpx();
                        Vector3D position = {actor->plane->x, actor->plane->y, actor->plane->z};
                        Vector3D orientation = {
                            actor->plane->azimuthf/10.0f + 90,
                            actor->plane->elevationf/10.0f,
                            -actor->plane->twist/10.0f
                        };
                        for (auto vertex: actor->plane->object->entity->vertices) {
                            if (vertex.x == bb->min.x) {
                                Renderer.drawPoint(vertex, {1.0f,0.0f,0.0f}, position, orientation);
                            }
                            if (vertex.x == bb->max.x) {
                                Renderer.drawPoint(vertex, {0.0f,1.0f,0.0f}, position, orientation);
                            }
                            if (vertex.z == bb->min.z) {
                                Renderer.drawPoint(vertex, {1.0f,0.0f,0.0f}, position, orientation);
                            }
                            if (vertex.z == bb->max.z) {
                                Renderer.drawPoint(vertex, {0.0f,1.0f,0.0f}, position, orientation);
                            }
                        }
                        Renderer.renderBBox(position, bb->min, bb->max);
                        Renderer.renderBBox(position+actor->formation_pos_offset, bb->min, bb->max);
                        Renderer.renderBBox(position+actor->attack_pos_offset, bb->min, bb->max);
                    } else if (!actor->has_exploded) {
                        // Once a fighter has exploded (has_exploded, set by
                        // hasBeenHit/the mission's destruction finalization
                        // — see [[project_wc3_hit_visuals_and_collision]]),
                        // its hull mesh stops rendering entirely — only the
                        // spawned SCDebris pieces (mission->debris, its own
                        // render loop below) represent it from then on,
                        // matching a real destroyed ship having nothing
                        // left to show but wreckage.
                        // Enemy ships fade to fully transparent on cloak,
                        // back to fully opaque on decloak (manual, 2026-07
                        // session) — SCMissionActors::UpdateCloak ramps
                        // plane->cloak_factor for every non-player actor;
                        // the player's own cloak instead drives a greyscale
                        // screen fade (WC3Strike::updateCloakEffect), not
                        // this. Restored to 1.0f right after so nothing
                        // else drawn later this frame (weapons, other
                        // actors, HUD) inherits the fade.
                        Renderer.modelAlphaMultiplier = 1.0f - actor->plane->cloak_factor;
                        actor->plane->Render();
                        Renderer.modelAlphaMultiplier = 1.0f;
                        if (actor->plane->alive) {
                            actor->plane->RenderSimulatedObject();
                        }
                        // Shield-hit visual — see the matching comment on
                        // the non-plane (capital ship) branch below for
                        // what this is. Same per-actor timer, just the
                        // plane's own position/orientation convention
                        // instead of MISN_PART's (matches renderPlaneLined's
                        // own bbox-orientation formula above).
                        if (actor->shield_hit_flash_timer > 0.0f) {
                            actor->shield_hit_flash_timer -= GameTimer::getInstance().getDeltaTime();
                            if (actor->plane->object->entity != nullptr && actor->plane->object->entity->shield != nullptr &&
                                actor->plane->object->entity->shield->objct != nullptr) {
                                Vector3D shieldPos = {actor->plane->x, actor->plane->y, actor->plane->z};
                                Vector3D shieldOrientation = {
                                    actor->plane->azimuthf / 10.0f + 90,
                                    actor->plane->elevationf / 10.0f,
                                    -actor->plane->twist / 10.0f
                                };
                                Renderer.drawModel(actor->plane->object->entity->shield->objct, Renderer.lodLevel, shieldPos, shieldOrientation);
                            }
                        }
                    }
                    if (actor->plane->object->alive == false) {
                        actor->plane->RenderSmoke();
                    }
                }
            } else if (actor->object->entity != nullptr) {
                Vector3D actor_position = {actor->object->position.x, actor->object->position.y, actor->object->position.z};
                Vector3D actor_orientation = {(360.0f - static_cast<float>(actor->object->azymuth) + 90.0f), static_cast<float>(actor->object->pitch), -static_cast<float>(actor->object->roll)};
                Vector3D distance = {
                    actor_position.x - this->player_plane->x,
                    actor_position.y - this->player_plane->y,
                    actor_position.z - this->player_plane->z
                };
                if (distance.Length() > RENDER_DISTANCE) {
                    continue; // Skip rendering if the object is too far away
                }
                // Capital ships (e.g. VICTORY.IFF) carry a separate flyable-into
                // hangar-bay interior model (RSEntity::flight_deck_entity, parsed
                // from APPR>POLY>SUPR). Its geometry is authored directly in the
                // parent ship's own local coordinate space (confirmed via
                // wc-iff-loader's OBJ export: the hangar submodel is merged with
                // translate=(0,0,0)/parent=-1, i.e. the ship's own frame, and its
                // vertex bounding box lines up with the ship's hull), so it's
                // rendered with the exact same world position/orientation as the
                // ship itself rather than any separate transform. The interior
                // tunnel mesh and the exterior hull occupy the same coordinate
                // space and overlap heavily (both span roughly the same length).
                // This used to pick one or the other based on proximity
                // (interior only, once inside HANGAR_INTERIOR_RADIUS) --
                // user-reported (2026-07 session) as "the Victory model is
                // disappearing" when flying into the hangar, since that
                // branch skipped the hull entirely. Hangar and hull must
                // always render together, so always draw both: the hangar
                // bay mouth is a genuine hole carved through the exterior
                // hull (that's how you fly into it) -- the hull mesh itself
                // has no geometry closing it off, since the original design
                // relies on the interior tunnel model being visible through
                // the opening. Draw the tunnel first so the hull (authored
                // to be viewed from outside) depth-occludes it everywhere
                // except the true opening, instead of the hole showing
                // nothing at hull-only render distance.
                if (actor->object->entity->flight_deck_entity != nullptr) {
                    Renderer.drawModel(actor->object->entity->flight_deck_entity, Renderer.lodLevel, actor_position, actor_orientation);
                }
                Renderer.drawModel(actor->object->entity, Renderer.lodLevel, actor_position, actor_orientation);
                // Shield-hit visual (RSEntity::shield->objct — SHLDFX, see
                // [[project_wc3_shield_effect_mesh]]): drawn at the same
                // transform as the hull itself for a brief flash whenever
                // hasBeenHit last absorbed a hit on shield. Whole-mesh, not
                // yet isolated to the actual hit facing (SHLDFX's own
                // GRUP facet groups, e.g. "SHIELD"/"TOPHALF", would be
                // needed for that — not investigated this session).
                if (actor->shield_hit_flash_timer > 0.0f) {
                    actor->shield_hit_flash_timer -= GameTimer::getInstance().getDeltaTime();
                    if (actor->object->entity->shield != nullptr && actor->object->entity->shield->objct != nullptr) {
                        Renderer.drawModel(actor->object->entity->shield->objct, Renderer.lodLevel, actor_position, actor_orientation);
                    }
                }
                if (this->show_bbox) {
                    if (actor->aiming_vector.x != 0.0f || actor->aiming_vector.y != 0.0f || actor->aiming_vector.z != 0.0f) {
                        Vector3D aim_pos = {actor->aiming_vector.x, actor->aiming_vector.y, actor->aiming_vector.z};
                        Renderer.drawLine(actor_position, aim_pos, {1.0f, 0.0f, 0.0f});
                    }
                }
                for (auto weapons : actor->weapons_shooted) {
                    if (weapons->alive) {
                        weapons->Render();
                    }
                }
                if (this->show_bbox) {
                    BoudingBox *bb = actor->object->entity->GetBoudingBpx();
                    Vector3D position = {actor->object->position.x, actor->object->position.y, actor->object->position.z};
                    Renderer.renderBBox(position, bb->min, bb->max);
                }
            }
        }

        for (auto expl: this->current_mission->explosions) {
            if (expl->is_finished) {
                // Remove explosion when finished
                this->current_mission->explosions.erase(
                    std::remove_if(
                        this->current_mission->explosions.begin(),
                        this->current_mission->explosions.end(),
                        [](const auto& expl) { return expl->is_finished; }
                    ),
                    this->current_mission->explosions.end());
                continue;
            }
            expl->render();
            if (expl->whiteoutRequested) {
                this->screen_whiteout_timer = 0.5f;
                expl->whiteoutRequested = false;
            }
        }
        if (this->screen_whiteout_timer > 0.0f) {
            this->screen_whiteout_timer -= GameTimer::getInstance().getDeltaTime();
            if (this->screen_whiteout_timer < 0.0f) {
                this->screen_whiteout_timer = 0.0f;
            }
        }

        float debrisDt = GameTimer::getInstance().getDeltaTime();
        for (auto piece: this->current_mission->debris) {
            if (piece->is_finished) {
                this->current_mission->debris.erase(
                    std::remove_if(
                        this->current_mission->debris.begin(),
                        this->current_mission->debris.end(),
                        [](const auto& piece) { return piece->is_finished; }
                    ),
                    this->current_mission->debris.end());
                continue;
            }
            piece->update(debrisDt);
            piece->render();
        }

        this->player_plane->RenderSimulatedObject();
        this->cockpit->cam = camera;

        // User-reported (2026-07 session): a green pixel/dot fixed at
        // screen center in every camera view. This was an always-on debug
        // marker — a point drawn 10 units directly ahead of the camera
        // along its own forward vector, which by construction always
        // projects to dead center regardless of camera mode. Not gated by
        // any debug flag, not part of the real boresight/reticle HUD
        // system (see SCCockpit's own kTargetingReticle/kCenterMarker for
        // that). Disabled rather than deleted outright in case it was
        // useful for future debugging — re-enable by uncommenting.
        // Vector3D centerPoint = camera->getPosition()+camera->getForward()*10.0f;
        // Renderer.drawPoint(centerPoint, {0.0f, 1.0f, 0.0f}, {0,0,0}, {0.0f, 0.0f, 0.0f});
        Renderer.drawPoint(this->cockpit->targetImpactPointWorld, {0.0f, 1.0f, 1.0f}, {0,0,0}, {0.0f, 0.0f, 0.0f});
        switch (this->camera_mode) {
        case View::MISSILE_CAM:
        case View::TARGET:
        case View::OBJECT:
        case View::AUTO_PILOT:
        case View::FOLLOW:
            if (!this->show_bbox) {
                this->player_plane->Render();
            } else {
                this->player_plane->renderPlaneLined();
            }
            break;
        case View::FRONT:
            if (!forceVirtualCockpit) {
                if (this->zoom_cockpit) {
                    this->cockpit->Render(CockpitFace::CP_BIG);
                } else {
                    this->cockpit->Render(CockpitFace::CP_FRONT);
                }
                break;
            }
        case View::RIGHT:
            if (!forceVirtualCockpit) {
                this->cockpit->Render(CockpitFace::CP_RIGHT);
                break;    
            }
        case View::LEFT:
            if (!forceVirtualCockpit) {
                this->cockpit->Render(CockpitFace::CP_LEFT);
                break;
            }
        case View::REAR:
            if (!forceVirtualCockpit) {
                this->cockpit->Render(CockpitFace::CP_REAR);
                break;
            }
        case View::EYE_ON_TARGET:
        case View::REAL:
        case View::CONTROLLER_LOOK:
            this->renderVirtualCockpit();
            break;
        }
        // Large-capital-ship death whiteout — see screen_whiteout_timer's
        // own comment (SCStrike.h). Drawn last so it covers the 3D scene
        // and the cockpit HUD just rendered above.
        if (this->screen_whiteout_timer > 0.0f) {
            Renderer.renderFullscreenFlash(1.0f, 1.0f, 1.0f, 1.0f);
        }
    };

    // VR stéréo: rendu par oeil + cockpit VR uniquement.
    bool vrShouldRender = false;
    const uint32_t eyeCount = Screen ? Screen->vrStereoEyeCount() : 0;
    if (eyeCount >= 2 && Screen->vrPrepareStereoFrame(vrShouldRender)) {
        if (!vrShouldRender) {
            Screen->vrEndStereoFrame();
            return;
        }

        const float metersToWorld = 1.0f;
        const float zNear = 1.5f;
        const float zFar = (float)(BLOCK_WIDTH * BLOCK_PER_MAP_SIDE * 4);

        const Point3D camPos = camera->getPosition();
        Quaternion camOri = camera->getOrientation();
        // Camera::orientation encode la rotation de vue (world->camera).
        // Pour OpenXR, on a besoin d'un repère "local" (cockpit) vers le monde.
        // Donc on prend l'inverse de la rotation (transpose pour une rotation pure).
        Matrix worldFromLocal = camOri.ToMatrix();
        worldFromLocal.Transpose();
        worldFromLocal.v[3][0] = camPos.x;
        worldFromLocal.v[3][1] = camPos.y;
        worldFromLocal.v[3][2] = camPos.z;
        worldFromLocal.v[3][3] = 1.0f;

        for (uint32_t eye = 0; eye < eyeCount; ++eye) {
            VRStereoEyeRenderInfo eyeInfo;
            if (!Screen->vrBeginStereoEye(eye, worldFromLocal, metersToWorld, zNear, zFar, eyeInfo)) {
                continue;
            }
            camera->setCustomMatrices(eyeInfo.projection, eyeInfo.view);
            this->cockpit->is_3d_cockpit = true;
            renderScene(eyeInfo.width, eyeInfo.height, 0.0f, true);

			if (this->cockpit && this->cockpit->target) {
				drawTargetSquareOverlay(
					eyeInfo.width,
					eyeInfo.height,
					eyeInfo.projection,
					eyeInfo.view,
					this->cockpit->target->position);
			}
        }

        camera->clearCustomMatrices();
        Screen->vrEndStereoFrame();
        return;
    }

    renderScene(Renderer.width, Renderer.height, verticalOffset, false);
}

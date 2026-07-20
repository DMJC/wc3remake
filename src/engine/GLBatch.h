#pragma once
#ifndef __APPLE__
#  ifndef _WIN32
#    ifndef GL_GLEXT_PROTOTYPES
#      define GL_GLEXT_PROTOTYPES
#    endif
#  endif
#  include <GL/gl.h>
#else
#  define GL_SILENCE_DEPRECATION
#  include <OpenGL/gl.h>
#endif
#include <vector>
#include <cstddef>

struct GBVert {
    float x, y, z;
    float s, t;
    float r, g, b, a;
};

// VBO-backed drop-in replacement for OpenGL immediate mode.
// Mirrors the glBegin/glEnd/glVertex*/glColor*/glTexCoord* state machine:
// color and texcoord are "current state" that persist between begin() calls,
// so color/texcoord set before begin() are applied to subsequent vertices.
//
//   gb.color4f(1,0,0,1);
//   gb.begin(GL_TRIANGLES);
//   gb.vertex3f(x,y,z);  // gets the red color set above
//   gb.end();             // uploads to VBO and draws
class GLBatch {
public:
    GLBatch();
    ~GLBatch();

    void begin(GLenum mode);

    void color3f(float r, float g, float b)          { m_r=r; m_g=g; m_b=b; m_a=1.f; }
    void color4f(float r, float g, float b, float a) { m_r=r; m_g=g; m_b=b; m_a=a; }

    void texCoord2f(float s, float t)                { m_s=s; m_t=t; }
    void texCoord2fv(const float* v)                 { m_s=v[0]; m_t=v[1]; }

    void color4fv(const float* v)                    { m_r=v[0]; m_g=v[1]; m_b=v[2]; m_a=v[3]; }

    void vertex2f(float  x, float  y)               { push(x, y, 0.f); }
    void vertex2d(double x, double y)               { push((float)x, (float)y, 0.f); }
    void vertex3f(float  x, float  y, float  z)     { push(x, y, z); }
    void vertex3d(double x, double y, double z)     { push((float)x, (float)y, (float)z); }

    void end();

private:
    GLuint  m_vbo{0};
    GLenum  m_mode{GL_TRIANGLES};
    float   m_r{1}, m_g{1}, m_b{1}, m_a{1};
    float   m_s{0}, m_t{0};
    std::vector<GBVert> m_verts;

    void ensureVBO();
    void push(float x, float y, float z);
};

extern GLBatch gb;

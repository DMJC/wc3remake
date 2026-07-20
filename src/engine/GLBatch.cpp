#include "GLBatch.h"
#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>
#include <cstddef>

GLBatch gb;

GLBatch::GLBatch() = default;

GLBatch::~GLBatch() {
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
}

void GLBatch::ensureVBO() {
    if (!m_vbo) glGenBuffers(1, &m_vbo);
}

void GLBatch::begin(GLenum mode) {
    m_mode = mode;
    m_verts.clear();
}

void GLBatch::push(float x, float y, float z) {
    m_verts.push_back({x, y, z, m_s, m_t, m_r, m_g, m_b, m_a});
}

void GLBatch::end() {
    if (m_verts.empty()) return;
    ensureVBO();

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(m_verts.size() * sizeof(GBVert)),
                 m_verts.data(),
                 GL_STREAM_DRAW);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    glVertexPointer  (3, GL_FLOAT, sizeof(GBVert), (const void*)offsetof(GBVert, x));
    glTexCoordPointer(2, GL_FLOAT, sizeof(GBVert), (const void*)offsetof(GBVert, s));
    glColorPointer   (4, GL_FLOAT, sizeof(GBVert), (const void*)offsetof(GBVert, r));

    glDrawArrays(m_mode, 0, (GLsizei)m_verts.size());

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

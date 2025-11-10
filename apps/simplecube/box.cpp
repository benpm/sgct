/*****************************************************************************************
 * SGCT                                                                                  *
 * Simple Graphics Cluster Toolkit                                                       *
 *                                                                                       *
 * Copyright (c) 2012-2025                                                               *
 * For conditions of distribution and use, see copyright notice in LICENSE.md            *
 ****************************************************************************************/

#include "box.h"

#include <sgct/sgct.h>
#include <sgct/user.h>
#include <sgct/opengl.h>
#include <array>

Box::Box(float size) {
    struct VertexData {
        float x = 0.f;
        float y = 0.f;
        float z = 0.f;
        float r = 0.f;
        float g = 0.f;
        float b = 0.f;
    };

    const float halfSize = size / 2.f;
    std::array<VertexData, 36> v;

    // Front face (+z) - Red
    v[0] = { -halfSize,  halfSize, halfSize, 1.f, 0.f, 0.f };
    v[1] = { -halfSize, -halfSize, halfSize, 1.f, 0.f, 0.f };
    v[2] = {  halfSize, -halfSize, halfSize, 1.f, 0.f, 0.f };
    v[3] = { -halfSize,  halfSize, halfSize, 1.f, 0.f, 0.f };
    v[4] = {  halfSize, -halfSize, halfSize, 1.f, 0.f, 0.f };
    v[5] = {  halfSize,  halfSize, halfSize, 1.f, 0.f, 0.f };

    // Right face (+x) - Green
    v[6] = { halfSize,  halfSize,  halfSize, 0.f, 1.f, 0.f };
    v[7] = { halfSize, -halfSize,  halfSize, 0.f, 1.f, 0.f };
    v[8] = { halfSize, -halfSize, -halfSize, 0.f, 1.f, 0.f };
    v[9] = { halfSize,  halfSize,  halfSize, 0.f, 1.f, 0.f };
    v[10] = { halfSize, -halfSize, -halfSize, 0.f, 1.f, 0.f };
    v[11] = { halfSize,  halfSize, -halfSize, 0.f, 1.f, 0.f };

    // Back face (-z) - Blue
    v[12] = {  halfSize,  halfSize, -halfSize, 0.f, 0.f, 1.f };
    v[13] = {  halfSize, -halfSize, -halfSize, 0.f, 0.f, 1.f };
    v[14] = { -halfSize, -halfSize, -halfSize, 0.f, 0.f, 1.f };
    v[15] = {  halfSize,  halfSize, -halfSize, 0.f, 0.f, 1.f };
    v[16] = { -halfSize, -halfSize, -halfSize, 0.f, 0.f, 1.f };
    v[17] = { -halfSize,  halfSize, -halfSize, 0.f, 0.f, 1.f };

    // Left face (-x) - Yellow
    v[18] = { -halfSize,  halfSize, -halfSize, 1.f, 1.f, 0.f };
    v[19] = { -halfSize, -halfSize, -halfSize, 1.f, 1.f, 0.f };
    v[20] = { -halfSize, -halfSize,  halfSize, 1.f, 1.f, 0.f };
    v[21] = { -halfSize,  halfSize, -halfSize, 1.f, 1.f, 0.f };
    v[22] = { -halfSize, -halfSize,  halfSize, 1.f, 1.f, 0.f };
    v[23] = { -halfSize,  halfSize,  halfSize, 1.f, 1.f, 0.f };

    // Top face (+y) - Magenta
    v[24] = { -halfSize, halfSize, -halfSize, 1.f, 0.f, 1.f };
    v[25] = { -halfSize, halfSize,  halfSize, 1.f, 0.f, 1.f };
    v[26] = {  halfSize, halfSize,  halfSize, 1.f, 0.f, 1.f };
    v[27] = { -halfSize, halfSize, -halfSize, 1.f, 0.f, 1.f };
    v[28] = {  halfSize, halfSize,  halfSize, 1.f, 0.f, 1.f };
    v[29] = {  halfSize, halfSize, -halfSize, 1.f, 0.f, 1.f };

    // Bottom face (-y) - Cyan
    v[30] = { -halfSize, -halfSize,  halfSize, 0.f, 1.f, 1.f };
    v[31] = { -halfSize, -halfSize, -halfSize, 0.f, 1.f, 1.f };
    v[32] = {  halfSize, -halfSize, -halfSize, 0.f, 1.f, 1.f };
    v[33] = { -halfSize, -halfSize,  halfSize, 0.f, 1.f, 1.f };
    v[34] = {  halfSize, -halfSize, -halfSize, 0.f, 1.f, 1.f };
    v[35] = {  halfSize, -halfSize,  halfSize, 0.f, 1.f, 1.f };

    glGenVertexArrays(1, &_vao);
    glBindVertexArray(_vao);

    glGenBuffers(1, &_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    constexpr int s = sizeof(VertexData);
    glBufferData(GL_ARRAY_BUFFER, v.size() * s, v.data(), GL_STATIC_DRAW);

    // Position attribute
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, s, nullptr);

    // Color attribute
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, s, reinterpret_cast<void*>(12));

    glBindVertexArray(0);
}

Box::~Box() {
    glDeleteVertexArrays(1, &_vao);
    glDeleteBuffers(1, &_vbo);
}

void Box::draw() const {
    glBindVertexArray(_vao);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
}

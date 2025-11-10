/*****************************************************************************************
 * SGCT                                                                                  *
 * Simple Graphics Cluster Toolkit                                                       *
 *                                                                                       *
 * Copyright (c) 2012-2025                                                               *
 * For conditions of distribution and use, see copyright notice in LICENSE.md            *
 ****************************************************************************************/

#include "box.h"
#include <sgct/sgct.h>
#include <sgct/opengl.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <string_view>
#include <vector>

namespace {
    std::vector<std::unique_ptr<Box>> boxes;
    GLint matrixLoc = -1;
    double currentTime = 0.0;

    constexpr std::string_view VertexShader = R"(
  #version 330 core

  layout(location = 0) in vec3 vertPosition;
  layout(location = 1) in vec3 vertColor;

  uniform mat4 mvp;
  out vec3 fragColor;

  void main() {
    gl_Position = mvp * vec4(vertPosition, 1.0);
    fragColor = vertColor;
  })";

    constexpr std::string_view FragmentShader = R"(
  #version 330 core

  in vec3 fragColor;
  out vec4 color;

  void main() { color = vec4(fragColor, 1.0); }
)";
} // namespace

using namespace sgct;

void initOGL(GLFWwindow*) {
    // Create a 4x4x4 grid of boxes around the origin
    constexpr int n = 8;
    constexpr float boxSize = 0.8f;
    constexpr float sep = 3.0f;

    for (int x = -n/2; x < n/2; x++) {
        for (int y = -n/2; y < n/2; y++) {
            for (int z = -n/2; z < n/2; z++) {
                if (x == 0 && y == 0 && z == 0) {
                    continue; // Skip the center box
                }
                glm::vec3 position = glm::vec3(
                    x * sep,
                    y * sep,
                    z * sep
                );
                boxes.push_back(std::make_unique<Box>(boxSize, position));
            }
        }
    }

    // Set up backface culling
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    ShaderManager::instance().addShaderProgram("xform", VertexShader, FragmentShader);
    const ShaderProgram& prg = ShaderManager::instance().shaderProgram("xform");
    prg.bind();
    matrixLoc = glGetUniformLocation(prg.id(), "mvp");
    prg.unbind();
}

void draw(const RenderData& data) {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    constexpr double Speed = 0.5;

    // Create base scene transform (animation and camera position)
    glm::mat4 scene = glm::translate(glm::mat4(1.f), glm::vec3(0.f, 0.f, -15.f));
    scene = glm::rotate(
        scene,
        static_cast<float>(currentTime * Speed),
        glm::vec3(0.f, 1.f, 0.f)
    );
    scene = glm::rotate(
        scene,
        static_cast<float>(currentTime * Speed * 0.5),
        glm::vec3(1.f, 0.f, 0.f)
    );

    ShaderManager::instance().shaderProgram("xform").bind();

    // Draw each box at its position
    for (const auto& box : boxes) {
        glm::mat4 boxTransform = glm::translate(scene, box->position());
        const glm::mat4 mvp =
            glm::make_mat4(data.modelViewProjectionMatrix.values.data()) * boxTransform;

        glUniformMatrix4fv(matrixLoc, 1, GL_FALSE, glm::value_ptr(mvp));
        box->draw();
    }

    ShaderManager::instance().shaderProgram("xform").unbind();

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
}

void preSync() {
    if (Engine::instance().isMaster()) {
        currentTime = time();
    }
}

std::vector<std::byte> encode() {
    std::vector<std::byte> data;
    serializeObject(data, currentTime);
    return data;
}

void decode(const std::vector<std::byte>& data) {
    unsigned int pos = 0;
    deserializeObject(data, pos, currentTime);
}

void cleanup() {
    boxes.clear();
}

void keyboard(Key key, Modifier, Action action, int, Window*) {
    if (key == Key::Esc && action == Action::Press) {
        Engine::instance().terminate();
    }
}

int main(int argc, char** argv) {
    std::vector<std::string> arg(argv + 1, argv + argc);
    Configuration config = parseArguments(arg);
    config::Cluster cluster = loadCluster(config.configFilename);
    if (!cluster.success) {
        return -1;
    }

    Engine::Callbacks callbacks;
    callbacks.initOpenGL = initOGL;
    callbacks.preSync = preSync;
    callbacks.encode = encode;
    callbacks.decode = decode;
    callbacks.draw = draw;
    callbacks.cleanup = cleanup;
    callbacks.keyboard = keyboard;

    try {
        Engine::create(cluster, callbacks, config);
    }
    catch (const std::runtime_error& e) {
        Log::Error(e.what());
        Engine::destroy();
        return EXIT_FAILURE;
    }

    Engine::instance().exec();
    Engine::destroy();
    return EXIT_SUCCESS;
}

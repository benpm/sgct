/*****************************************************************************************
 * SGCT                                                                                  *
 * Simple Graphics Cluster Toolkit                                                       *
 *                                                                                       *
 * Copyright (c) 2012-2025                                                               *
 * For conditions of distribution and use, see copyright notice in LICENSE.md            *
 ****************************************************************************************/

#include "box.h"
#include "bloom_effect.h"
#include <sgct/sgct.h>
#include <sgct/opengl.h>
#include <sgct/projection/fisheye.h>
#include <sgct/projection/nonlinearprojection.h>
#include <sgct/user.h>
#include <sgct/shadermanager.h>
#include <sgct/engine.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <format>
#include <fstream>
#include <memory>
#include <sstream>
#include <string_view>
#include <vector>

namespace {
    std::vector<std::unique_ptr<Box>> boxes;
    GLint matrixLoc = -1;
    double currentTime = 0.0;
    bool firstFrameValidated = false;
    bool testingMode = false;
    std::unique_ptr<BloomEffect> bloomEffect;
    bool enableBloom = true;

    std::string loadShaderFile(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            sgct::Log::Error(std::format("Failed to open shader file: {}", filename));
            return "";
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

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

    GLint postProcessTexLoc = -1;
    GLint passthroughTexLoc = -1;
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

    // Initialize post-process shaders from files
    const std::string shaderPath = "apps/simplecube/shaders/";
    const std::string postVertSrc = loadShaderFile(shaderPath + "postprocess_vertex.vert");
    const std::string postFragSrc = loadShaderFile(shaderPath + "postprocess_invert.frag");
    const std::string passthroughFragSrc = loadShaderFile(shaderPath + "passthrough.frag");

    ShaderManager::instance().addShaderProgram("postprocess", postVertSrc, postFragSrc);
    const ShaderProgram& ppPrg = ShaderManager::instance().shaderProgram("postprocess");
    ppPrg.bind();
    postProcessTexLoc = glGetUniformLocation(ppPrg.id(), "tex");
    glUniform1i(postProcessTexLoc, 0); // Texture unit 0
    ppPrg.unbind();

    // Initialize passthrough shader for bloom output
    ShaderManager::instance().addShaderProgram("passthrough", postVertSrc, passthroughFragSrc);
    const ShaderProgram& passPrg = ShaderManager::instance().shaderProgram("passthrough");
    passPrg.bind();
    passthroughTexLoc = glGetUniformLocation(passPrg.id(), "tex");
    glUniform1i(passthroughTexLoc, 0); // Texture unit 0
    passPrg.unbind();

    // Initialize bloom effect
    const ivec2 windowSize = Engine::instance().thisNode().windows()[0]->framebufferResolution();
    bloomEffect = std::make_unique<BloomEffect>(windowSize.x, windowSize.y);
    Log::Info(std::format("Bloom effect initialized for window size {}x{}", windowSize.x, windowSize.y));
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

void postDraw() {
    // Validate first frame rendering (only in testing mode)
    if (testingMode && !firstFrameValidated) {
        const Window& window = *Engine::instance().thisNode().windows()[0];
        const ivec2 size = window.framebufferResolution();
        
        // Read framebuffer contents
        std::vector<unsigned char> pixels(size.x * size.y * 3);
        glReadPixels(0, 0, size.x, size.y, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        
        // Check if all pixels are the same color (indicating failed render)
        bool allSameColor = true;
        unsigned char r0 = pixels[0];
        unsigned char g0 = pixels[1];
        unsigned char b0 = pixels[2];
        
        for (size_t i = 3; i < pixels.size(); i += 3) {
            if (pixels[i] != r0 || pixels[i+1] != g0 || pixels[i+2] != b0) {
                allSameColor = false;
                break;
            }
        }
        
        // Write result
        std::ofstream resultFile("frame_test_result.txt");
        if (allSameColor) {
            resultFile << "FAIL: First frame is solid color RGB(" 
                      << static_cast<int>(r0) << ", " 
                      << static_cast<int>(g0) << ", " 
                      << static_cast<int>(b0) << ")\n";
            Log::Error("Frame test FAILED - solid color detected");
        } else {
            resultFile << "PASS: First frame rendered with varying colors\n";
            Log::Info("Frame test PASSED - frame rendered correctly");
        }
        resultFile.close();
        
        firstFrameValidated = true;
        
        // Exit after first frame test
        Engine::instance().terminate();
    }
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

void postProcess(const Window& window, FrustumMode, unsigned int inputTexture, ivec2 size) {
    if (enableBloom && bloomEffect) {
        // Render bloom effect (internally composites with original scene)
        bloomEffect->render(inputTexture, size);

        // Bloom effect outputs composited result, render to current framebuffer
        GLuint bloomOutput = bloomEffect->outputTexture();

        // Render full-screen quad with bloom output using passthrough shader
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, bloomOutput);
        
        const ShaderProgram& passPrg = ShaderManager::instance().shaderProgram("passthrough");
        passPrg.bind();
        window.renderScreenQuad();
        passPrg.unbind();
    } else {
        // Original behavior: simple pass-through or color inversion
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTexture);

        const ShaderProgram& ppPrg = ShaderManager::instance().shaderProgram("postprocess");
        ppPrg.bind();
        window.renderScreenQuad();
        ppPrg.unbind();
    }
}

void cleanup() {
    boxes.clear();
    bloomEffect.reset();
}

void keyboard(Key key, Modifier, Action action, int, Window*) {
    if (action == Action::Press) {
        if (key == Key::Esc) {
            Engine::instance().terminate();
        } else if (key == Key::B) {
            enableBloom = !enableBloom;
            Log::Info(std::format("Bloom: {}", enableBloom ? "ON" : "OFF"));
        } else if (key == Key::Up && bloomEffect) {
            auto& settings = bloomEffect->settings();
            settings.bloomStrength = std::min(settings.bloomStrength + 0.01f, 1.0f);
            Log::Info(std::format("Bloom strength: {:.3f}", settings.bloomStrength));
        } else if (key == Key::Down && bloomEffect) {
            auto& settings = bloomEffect->settings();
            settings.bloomStrength = std::max(settings.bloomStrength - 0.01f, 0.0f);
            Log::Info(std::format("Bloom strength: {:.3f}", settings.bloomStrength));
        } else if (key == Key::T && bloomEffect) {
            auto& settings = bloomEffect->settings();
            settings.threshold += 0.1f;
            Log::Info(std::format("Bloom threshold: {:.2f}", settings.threshold));
        } else if (key == Key::G && bloomEffect) {
            auto& settings = bloomEffect->settings();
            settings.threshold = std::max(settings.threshold - 0.1f, 0.0f);
            Log::Info(std::format("Bloom threshold: {:.2f}", settings.threshold));
        }
    }
}

int main(int argc, char** argv) {
    std::vector<std::string> arg(argv + 1, argv + argc);
    
    // Check for --testing flag
    auto it = std::find(arg.begin(), arg.end(), "--testing");
    if (it != arg.end()) {
        testingMode = true;
        arg.erase(it);  // Remove --testing so parseArguments doesn't choke
        Log::Info("Running in testing mode - will exit after first frame");
    }
    
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
    callbacks.postDraw = postDraw;
    callbacks.postProcess = postProcess;

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

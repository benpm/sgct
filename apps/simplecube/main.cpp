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
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
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
    GLint modelMatrixLoc = -1;
    GLint normalMatrixLoc = -1;
    GLint lightPosLoc = -1;
    GLint lightPosLoc = -1;
    GLint viewPosLoc = -1;
    GLint brightnessLoc = -1;
    float sceneBrightness = 1.0f;
    double currentTime = 0.0;
    bool firstFrameValidated = false;
    bool testingMode = false;
    std::unique_ptr<BloomEffect> bloomEffect;
    bool enableBloom = true;
    bool showUI = true;

    // Debug view modes
    enum class ViewMode {
        Composited,      // Normal view with bloom composited
        BloomOnly,       // Just the bloom contribution (upsampled)
        BrightPass,      // The threshold extraction result
        BlurPass,        // The mip-sampled blur result
        SceneOnly        // Original scene without bloom
    };
    ViewMode currentViewMode = ViewMode::Composited;
    const char* viewModeNames[] = {
        "Composited (Normal)",
        "Bloom Only",
        "Bright Pass",
        "Blur Pass",
        "Scene Only (No Bloom)"
    };

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
  layout(location = 1) in vec3 vertNormal;
  layout(location = 2) in vec3 vertColor;

  uniform mat4 mvp;
  uniform mat4 modelMatrix;
  uniform mat3 normalMatrix;

  out vec3 fragPosition;
  out vec3 fragNormal;
  out vec3 fragColor;

  void main() {
    gl_Position = mvp * vec4(vertPosition, 1.0);
    fragPosition = vec3(modelMatrix * vec4(vertPosition, 1.0));
    fragNormal = normalize(normalMatrix * vertNormal);
    fragColor = vertColor;
  })";

    constexpr std::string_view FragmentShader = R"(
  #version 330 core

  in vec3 fragPosition;
  in vec3 fragNormal;
  in vec3 fragColor;
  out vec4 color;

  out vec4 color;

  uniform float brightness;
  uniform vec3 lightPos;
  uniform vec3 viewPos;

  void main() {
    // Ambient lighting (low intensity)
    float ambientStrength = 0.1;
    vec3 ambient = ambientStrength * fragColor;

    // Diffuse lighting (main light source - 10x ambient)
    vec3 norm = normalize(fragNormal);
    vec3 lightDir = normalize(lightPos - fragPosition);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * fragColor;

    // Specular lighting (bright highlights)
    float specularStrength = 0.5;
    vec3 viewDir = normalize(viewPos - fragPosition);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
    vec3 specular = specularStrength * spec * vec3(1.0, 1.0, 1.0);

    // Combine all components
    // Combine all components
    vec3 result = (ambient + diffuse + specular) * brightness;
    color = vec4(result, 1.0);
  }
)";

    GLint postProcessTexLoc = -1;
    GLint passthroughTexLoc = -1;

    // ImGui state
    GLFWwindow* imguiWindow = nullptr;
} // namespace

using namespace sgct;

void initOGL(GLFWwindow* sharedWindow) {
    // Store window for ImGui
    imguiWindow = sharedWindow;

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Setup ImGui style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.FrameRounding = 3.0f;
    style.Alpha = 0.95f;

    // Initialize ImGui backends
    ImGui_ImplGlfw_InitForOpenGL(sharedWindow, false);  // false = don't install callbacks
    ImGui_ImplOpenGL3_Init("#version 330");

    Log::Info("ImGui initialized");

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
    modelMatrixLoc = glGetUniformLocation(prg.id(), "modelMatrix");
    normalMatrixLoc = glGetUniformLocation(prg.id(), "normalMatrix");
    lightPosLoc = glGetUniformLocation(prg.id(), "lightPos");
    lightPosLoc = glGetUniformLocation(prg.id(), "lightPos");
    viewPosLoc = glGetUniformLocation(prg.id(), "viewPos");
    brightnessLoc = glGetUniformLocation(prg.id(), "brightness");
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
    glUniform1f(brightnessLoc, sceneBrightness);

    // Set light position (bright light above and to the side)
    glm::vec3 lightPos(10.f, 15.f, 5.f);
    glUniform3fv(lightPosLoc, 1, glm::value_ptr(lightPos));

    // Set view position (camera position)
    glm::vec3 viewPos(0.f, 0.f, 0.f);
    glUniform3fv(viewPosLoc, 1, glm::value_ptr(viewPos));

    // Draw each box at its position
    for (const auto& box : boxes) {
        glm::mat4 boxTransform = glm::translate(scene, box->position());
        const glm::mat4 mvp =
            glm::make_mat4(data.modelViewProjectionMatrix.values.data()) * boxTransform;

        // Calculate normal matrix (inverse transpose of model matrix)
        glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(boxTransform)));

        glUniformMatrix4fv(matrixLoc, 1, GL_FALSE, glm::value_ptr(mvp));
        glUniformMatrix4fv(modelMatrixLoc, 1, GL_FALSE, glm::value_ptr(boxTransform));
        glUniformMatrix3fv(normalMatrixLoc, 1, GL_FALSE, glm::value_ptr(normalMatrix));
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
        // Always render bloom effect to have intermediate textures available
        bloomEffect->render(inputTexture, size);

        // Select texture based on view mode
        GLuint textureToDisplay = 0;
        switch (currentViewMode) {
            case ViewMode::Composited:
                textureToDisplay = bloomEffect->outputTexture();
                break;
            case ViewMode::BloomOnly:
                textureToDisplay = bloomEffect->upsampleTexture();
                break;
            case ViewMode::BrightPass:
                textureToDisplay = bloomEffect->brightPassTexture();
                break;
            case ViewMode::BlurPass:
                textureToDisplay = bloomEffect->blurTexture();
                break;
            case ViewMode::SceneOnly:
                textureToDisplay = inputTexture;
                break;
        }

        // Render full-screen quad with selected texture using passthrough shader
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureToDisplay);

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

void draw2D(const RenderData&) {
    if (!showUI || !imguiWindow) {
        return;
    }

    // Start ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Bloom Control Panel
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 400), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Bloom Controls", &showUI)) {
        // Enable/Disable bloom
        ImGui::Checkbox("Enable Bloom", &enableBloom);
        ImGui::Separator();

        if (enableBloom && bloomEffect) {
            auto& settings = bloomEffect->settings();

            // View Mode selector
            ImGui::Text("View Mode:");
            int viewModeInt = static_cast<int>(currentViewMode);
            if (ImGui::Combo("##viewmode", &viewModeInt, viewModeNames, IM_ARRAYSIZE(viewModeNames))) {
                currentViewMode = static_cast<ViewMode>(viewModeInt);
            }
            ImGui::Separator();

            // Threshold controls
            ImGui::Text("Threshold Settings");
            ImGui::SliderFloat("Threshold", &settings.threshold, 0.0f, 5.0f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Luminance threshold for bloom extraction");
            }

            ImGui::SliderFloat("Soft Threshold", &settings.softThreshold, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Smoothness of threshold transition");
            }
            ImGui::Separator();

            // Bloom intensity controls
            ImGui::Text("Bloom Intensity");
            ImGui::SliderFloat("Strength", &settings.bloomStrength, 0.0f, 1.0f, "%.3f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Final bloom contribution to the image");
            }

            ImGui::SliderFloat("Max Brightness", &settings.maxBrightness, 1.0f, 50.0f, "%.1f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Clamp bright values to prevent fireflies");
            }

            ImGui::SliderFloat("Exposure", &settings.exposure, 0.1f, 5.0f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Scene exposure before tone mapping");
            }

            ImGui::SliderFloat("Gamma", &settings.gamma, 0.1f, 5.0f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Gamma correction value (default 2.2)");
            }
            ImGui::Separator();

            // Quality settings
            ImGui::Text("Quality Settings");
            ImGui::SliderInt("Mip Levels", &settings.mipLevels, 1, 10);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Number of mip levels to sample (more = wider blur)");
            }

            ImGui::Checkbox("Use Tent Filter", &settings.useTentFilter);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Enable tent filter for smoother upsampling");
            }
            ImGui::Separator();

            // Preset buttons
            ImGui::Text("Presets");
            if (ImGui::Button("Subtle")) {
                settings.threshold = 1.5f;
                settings.softThreshold = 0.5f;
                settings.bloomStrength = 0.02f;
                settings.maxBrightness = 10.0f;
            }
            ImGui::SameLine();
            if (ImGui::Button("Normal")) {
                settings.threshold = 1.0f;
                settings.softThreshold = 0.5f;
                settings.bloomStrength = 0.04f;
                settings.maxBrightness = 10.0f;
            }
            ImGui::SameLine();
            if (ImGui::Button("Strong")) {
                settings.threshold = 0.5f;
                settings.softThreshold = 0.3f;
                settings.bloomStrength = 0.1f;
                settings.maxBrightness = 20.0f;
            }
            ImGui::SameLine();
            if (ImGui::Button("Dreamy")) {
                settings.threshold = 0.2f;
                settings.softThreshold = 0.8f;
                settings.bloomStrength = 0.15f;
                settings.maxBrightness = 5.0f;
            }
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Bloom is disabled");
        }

        ImGui::Separator();
        ImGui::Text("Press H to toggle this panel");
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

        ImGui::Separator();
        ImGui::Text("Scene Settings");
        ImGui::SliderFloat("Scene Brightness", &sceneBrightness, 0.0f, 5.0f, "%.2f");
    }
    ImGui::End();

    // Render ImGui
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void cleanup() {
    // Cleanup ImGui
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    boxes.clear();
    bloomEffect.reset();
}

void keyboard(Key key, Modifier, Action action, int scancode, Window*) {
    // Forward to ImGui
    if (imguiWindow) {
        int glfwAction = (action == Action::Press) ? GLFW_PRESS :
                         (action == Action::Release) ? GLFW_RELEASE : GLFW_REPEAT;
        ImGui_ImplGlfw_KeyCallback(imguiWindow, static_cast<int>(key), scancode, glfwAction, 0);
    }

    // Check if ImGui wants keyboard input
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard) {
        return;
    }

    if (action == Action::Press) {
        if (key == Key::Esc) {
            Engine::instance().terminate();
        } else if (key == Key::H) {
            showUI = !showUI;
            Log::Info(std::format("UI: {}", showUI ? "ON" : "OFF"));
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

void mouseButton(MouseButton button, Modifier, Action action, Window*) {
    // Forward to ImGui
    if (imguiWindow) {
        int glfwButton = static_cast<int>(button);
        int glfwAction = (action == Action::Press) ? GLFW_PRESS : GLFW_RELEASE;
        ImGui_ImplGlfw_MouseButtonCallback(imguiWindow, glfwButton, glfwAction, 0);
    }
}

void mousePos(double x, double y, Window*) {
    // Forward to ImGui
    if (imguiWindow) {
        ImGui_ImplGlfw_CursorPosCallback(imguiWindow, x, y);
    }
}

void mouseScroll(double xOffset, double yOffset, Window*) {
    // Forward to ImGui
    if (imguiWindow) {
        ImGui_ImplGlfw_ScrollCallback(imguiWindow, xOffset, yOffset);
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
    callbacks.draw2D = draw2D;
    callbacks.cleanup = cleanup;
    callbacks.keyboard = keyboard;
    callbacks.mouseButton = mouseButton;
    callbacks.mousePos = mousePos;
    callbacks.mouseScroll = mouseScroll;
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

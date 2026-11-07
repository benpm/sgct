/*****************************************************************************************
 * SGCT                                                                                  *
 * Simple Graphics Cluster Toolkit                                                       *
 *                                                                                       *
 * Copyright (c) 2012-2026                                                               *
 * For conditions of distribution and use, see copyright notice in LICENSE.md            *
 ****************************************************************************************/

// Renders a grid of lit cubes and demonstrates the Engine::Callbacks::postProcess hook by
// applying a bloom effect to the finished frame. An ImGui panel controls the effect.

#include "bloom_effect.h"
#include "box.h"
#include <sgct/sgct.h>
#include <sgct/opengl.h>
#include <sgct/shadermanager.h>
#include <sgct/user.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <string_view>
#include <vector>

namespace {
    std::vector<std::unique_ptr<Box>> boxes;
    GLint matrixLoc = -1;
    GLint modelMatrixLoc = -1;
    GLint normalMatrixLoc = -1;
    GLint lightPosLoc = -1;
    GLint viewPosLoc = -1;
    GLint brightnessLoc = -1;
    float sceneBrightness = 1.f;
    double currentTime = 0.0;

    std::unique_ptr<BloomEffect> bloomEffect;
    bool enableBloom = true;
    bool showUI = true;
    GLFWwindow* imguiWindow = nullptr;

    // Which intermediate result of the bloom effect is shown
    enum class ViewMode {
        Composited,
        BloomOnly,
        BrightPass,
        BlurPass,
        SceneOnly
    };
    ViewMode currentViewMode = ViewMode::Composited;
    constexpr std::array<const char*, 5> ViewModeNames = {
        "Composited (Normal)",
        "Bloom Only",
        "Bright Pass",
        "Blur Pass",
        "Scene Only (No Bloom)"
    };

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

  uniform float brightness;
  uniform vec3 lightPos;
  uniform vec3 viewPos;

  void main() {
    vec3 ambient = 0.1 * fragColor;

    vec3 norm = normalize(fragNormal);
    vec3 lightDir = normalize(lightPos - fragPosition);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * fragColor;

    vec3 viewDir = normalize(viewPos - fragPosition);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
    vec3 specular = 0.5 * spec * vec3(1.0);

    color = vec4((ambient + diffuse + specular) * brightness, 1.0);
  })";

    // Draws a texture over the whole viewport
    constexpr std::string_view PassthroughVert = R"(
  #version 330 core

  layout (location = 0) in vec3 in_position;
  layout (location = 1) in vec2 in_texCoords;

  out vec2 uv;

  void main() {
    gl_Position = vec4(in_position, 1.0);
    uv = in_texCoords;
  })";

    constexpr std::string_view PassthroughFrag = R"(
  #version 330 core

  uniform sampler2D tex;
  in vec2 uv;
  out vec4 color;

  void main() {
    color = texture(tex, uv);
  })";
} // namespace

using namespace sgct;

void initOGL(GLFWwindow* sharedWindow) {
    imguiWindow = sharedWindow;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.f;
    style.FrameRounding = 3.f;
    style.Alpha = 0.95f;
    // SGCT owns the GLFW callbacks and forwards the events below
    ImGui_ImplGlfw_InitForOpenGL(sharedWindow, false);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Create a grid of boxes around the origin
    constexpr int N = 8;
    constexpr float BoxSize = 0.8f;
    constexpr float Separation = 3.f;
    for (int x = -N / 2; x < N / 2; x++) {
        for (int y = -N / 2; y < N / 2; y++) {
            for (int z = -N / 2; z < N / 2; z++) {
                if (x == 0 && y == 0 && z == 0) {
                    continue;
                }
                const glm::vec3 position = glm::vec3(x, y, z) * Separation;
                boxes.push_back(std::make_unique<Box>(BoxSize, position));
            }
        }
    }

    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    ShaderManager::instance().addShaderProgram("xform", VertexShader, FragmentShader);
    const ShaderProgram& prg = ShaderManager::instance().shaderProgram("xform");
    prg.bind();
    matrixLoc = glGetUniformLocation(prg.id(), "mvp");
    modelMatrixLoc = glGetUniformLocation(prg.id(), "modelMatrix");
    normalMatrixLoc = glGetUniformLocation(prg.id(), "normalMatrix");
    lightPosLoc = glGetUniformLocation(prg.id(), "lightPos");
    viewPosLoc = glGetUniformLocation(prg.id(), "viewPos");
    brightnessLoc = glGetUniformLocation(prg.id(), "brightness");
    prg.unbind();

    ShaderManager::instance().addShaderProgram(
        "passthrough",
        PassthroughVert,
        PassthroughFrag
    );
    const ShaderProgram& passPrg = ShaderManager::instance().shaderProgram("passthrough");
    passPrg.bind();
    glUniform1i(glGetUniformLocation(passPrg.id(), "tex"), 0);
    passPrg.unbind();

    // The effect resizes itself to whatever texture it is handed, so the initial size
    // only has to be a reasonable guess
    const glm::ivec2 size =
        Engine::instance().thisNode().windows()[0]->framebufferResolution();
    bloomEffect = std::make_unique<BloomEffect>(size.x, size.y);
}

void draw(const RenderData& data) {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    constexpr double Speed = 0.5;
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
    const glm::vec3 lightPos = glm::vec3(10.f, 15.f, 5.f);
    glUniform3fv(lightPosLoc, 1, glm::value_ptr(lightPos));
    const glm::vec3 viewPos = glm::vec3(0.f);
    glUniform3fv(viewPosLoc, 1, glm::value_ptr(viewPos));

    for (const std::unique_ptr<Box>& box : boxes) {
        const glm::mat4 boxTransform = glm::translate(scene, box->position());
        const glm::mat4 mvp = data.modelViewProjectionMatrix * boxTransform;
        const glm::mat3 normalMatrix =
            glm::transpose(glm::inverse(glm::mat3(boxTransform)));

        glUniformMatrix4fv(matrixLoc, 1, GL_FALSE, glm::value_ptr(mvp));
        glUniformMatrix4fv(modelMatrixLoc, 1, GL_FALSE, glm::value_ptr(boxTransform));
        glUniformMatrix3fv(normalMatrixLoc, 1, GL_FALSE, glm::value_ptr(normalMatrix));
        box->draw();
    }

    ShaderManager::instance().shaderProgram("xform").unbind();

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
}

void postProcess(const Window& window, FrustumMode, unsigned int inputTexture,
                 glm::ivec2 size)
{
    // SGCT has bound the framebuffer that the result has to end up in, so all that is
    // left after running the effect is to draw the selected texture into it
    GLuint textureToDisplay = inputTexture;
    if (enableBloom && bloomEffect) {
        bloomEffect->render(inputTexture, size);
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
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureToDisplay);
    const ShaderProgram& passPrg = ShaderManager::instance().shaderProgram("passthrough");
    passPrg.bind();
    window.renderScreenQuad();
    passPrg.unbind();
}

void draw2D(const RenderData&) {
    if (!showUI || !imguiWindow) {
        return;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Bloom Controls", &showUI)) {
        ImGui::Checkbox("Enable Bloom", &enableBloom);

        if (enableBloom && bloomEffect) {
            BloomEffect::Settings& settings = bloomEffect->settings();
            ImGui::Separator();
            ImGui::Text("Debug View");
            int viewMode = static_cast<int>(currentViewMode);
            if (ImGui::Combo("Mode", &viewMode, ViewModeNames.data(), 5)) {
                currentViewMode = static_cast<ViewMode>(viewMode);
            }

            ImGui::Separator();
            ImGui::Text("Threshold");
            ImGui::SliderFloat("Luminance", &settings.threshold, 0.f, 5.f, "%.2f");
            ImGui::SliderFloat("Softness", &settings.softThreshold, 0.f, 2.f, "%.2f");
            ImGui::SliderFloat("Max Brightness", &settings.maxBrightness, 1.f, 50.f);

            ImGui::Separator();
            ImGui::Text("Bloom");
            ImGui::SliderFloat("Strength", &settings.bloomStrength, 0.f, 1.f, "%.3f");
            ImGui::SliderInt("Mip Levels", &settings.mipLevels, 1, 10);

            ImGui::Separator();
            ImGui::Text("Tone Mapping");
            ImGui::SliderFloat("Exposure", &settings.exposure, 0.1f, 5.f, "%.2f");
            ImGui::SliderFloat("Gamma", &settings.gamma, 1.f, 3.f, "%.2f");

            ImGui::Separator();
            ImGui::Text("Presets");
            if (ImGui::Button("Subtle")) {
                settings.threshold = 1.5f;
                settings.softThreshold = 0.5f;
                settings.bloomStrength = 0.02f;
                settings.maxBrightness = 10.f;
            }
            ImGui::SameLine();
            if (ImGui::Button("Normal")) {
                settings.threshold = 1.f;
                settings.softThreshold = 0.5f;
                settings.bloomStrength = 0.04f;
                settings.maxBrightness = 10.f;
            }
            ImGui::SameLine();
            if (ImGui::Button("Strong")) {
                settings.threshold = 0.5f;
                settings.softThreshold = 0.3f;
                settings.bloomStrength = 0.1f;
                settings.maxBrightness = 20.f;
            }
            ImGui::SameLine();
            if (ImGui::Button("Dreamy")) {
                settings.threshold = 0.2f;
                settings.softThreshold = 0.8f;
                settings.bloomStrength = 0.15f;
                settings.maxBrightness = 5.f;
            }
        }
        else {
            ImGui::TextColored(ImVec4(1.f, 0.5f, 0.f, 1.f), "Bloom is disabled");
        }

        ImGui::Separator();
        ImGui::Text("Press H to toggle this panel, B to toggle bloom");
        ImGui::Text("FPS: %.1f", static_cast<double>(ImGui::GetIO().Framerate));

        ImGui::Separator();
        ImGui::SliderFloat("Scene Brightness", &sceneBrightness, 0.f, 5.f, "%.2f");
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
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
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    boxes.clear();
    bloomEffect.reset();
}

void keyboard(Key key, Modifier, Action action, int scancode, Window*) {
    if (imguiWindow) {
        const int glfwAction =
            (action == Action::Press) ? GLFW_PRESS :
            (action == Action::Release) ? GLFW_RELEASE : GLFW_REPEAT;
        ImGui_ImplGlfw_KeyCallback(
            imguiWindow,
            static_cast<int>(key),
            scancode,
            glfwAction,
            0
        );
    }
    if (ImGui::GetIO().WantCaptureKeyboard || action != Action::Press) {
        return;
    }

    switch (key) {
        case Key::Esc:
            Engine::instance().terminate();
            break;
        case Key::H:
            showUI = !showUI;
            break;
        case Key::B:
            enableBloom = !enableBloom;
            Log::Info(std::format("Bloom: {}", enableBloom ? "on" : "off"));
            break;
        case Key::Up:
            if (bloomEffect) {
                BloomEffect::Settings& s = bloomEffect->settings();
                s.bloomStrength = std::min(s.bloomStrength + 0.01f, 1.f);
                Log::Info(std::format("Bloom strength: {:.3f}", s.bloomStrength));
            }
            break;
        case Key::Down:
            if (bloomEffect) {
                BloomEffect::Settings& s = bloomEffect->settings();
                s.bloomStrength = std::max(s.bloomStrength - 0.01f, 0.f);
                Log::Info(std::format("Bloom strength: {:.3f}", s.bloomStrength));
            }
            break;
        default:
            break;
    }
}

void mouseButton(MouseButton button, Modifier, Action action, Window*) {
    if (imguiWindow) {
        const int glfwAction = (action == Action::Press) ? GLFW_PRESS : GLFW_RELEASE;
        ImGui_ImplGlfw_MouseButtonCallback(
            imguiWindow,
            static_cast<int>(button),
            glfwAction,
            0
        );
    }
}

void mousePos(double x, double y, Window*) {
    if (imguiWindow) {
        ImGui_ImplGlfw_CursorPosCallback(imguiWindow, x, y);
    }
}

void mouseScroll(double xOffset, double yOffset, Window*) {
    if (imguiWindow) {
        ImGui_ImplGlfw_ScrollCallback(imguiWindow, xOffset, yOffset);
    }
}

int main(int argc, char** argv) {
    std::vector<std::string> arg(argv + 1, argv + argc);
    const Configuration config = parseArguments(arg);
    const config::Cluster cluster = loadCluster(config.configFilename);
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
    callbacks.postProcess = postProcess;
    callbacks.cleanup = cleanup;
    callbacks.keyboard = keyboard;
    callbacks.mouseButton = mouseButton;
    callbacks.mousePos = mousePos;
    callbacks.mouseScroll = mouseScroll;

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

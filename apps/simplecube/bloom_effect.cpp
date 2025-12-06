/*****************************************************************************************
 * SGCT                                                                                  *
 * Simple Graphics Cluster Toolkit                                                       *
 *                                                                                       *
 * Copyright (c) 2012-2025                                                               *
 * For conditions of distribution and use, see copyright notice in LICENSE.md            *
 ****************************************************************************************/

#include "bloom_effect.h"
#include <sgct/log.h>
#include <array>
#include <format>
#include <fstream>
#include <sstream>

namespace {
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

    GLuint compileShader(const std::string& vertexPath, const std::string& fragmentPath) {
        std::string vertexSrc = loadShaderFile(vertexPath);
        std::string fragmentSrc = loadShaderFile(fragmentPath);
        
        if (vertexSrc.empty() || fragmentSrc.empty()) {
            sgct::Log::Error("Shader source is empty");
            return 0;
        }

        const char* vertSrcPtr = vertexSrc.c_str();
        const char* fragSrcPtr = fragmentSrc.c_str();

        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &vertSrcPtr, nullptr);
        glCompileShader(vertexShader);

        GLint success = 0;
        glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            GLchar infoLog[512];
            glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
            sgct::Log::Error(std::format("Bloom vertex shader compilation failed ({}): {}", vertexPath, infoLog));
        }

        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &fragSrcPtr, nullptr);
        glCompileShader(fragmentShader);

        glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            GLchar infoLog[512];
            glGetShaderInfoLog(fragmentShader, 512, nullptr, infoLog);
            sgct::Log::Error(std::format("Bloom fragment shader compilation failed ({}): {}", fragmentPath, infoLog));
        }

        GLuint program = glCreateProgram();
        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);
        glLinkProgram(program);

        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            GLchar infoLog[512];
            glGetProgramInfoLog(program, 512, nullptr, infoLog);
            sgct::Log::Error(std::format("Bloom shader linking failed: {}", infoLog));
        }

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        return program;
    }
}

BloomEffect::BloomEffect(int width, int height)
    : _width(width)
    , _height(height)
{
    createFramebuffers(width, height);
    compileShaders();

    // Create full-screen quad
    std::array<float, 20> quadVertices = {
        // positions        // texcoords
        -1.0f,  1.0f, 0.0f,  0.0f, 1.0f,
        -1.0f, -1.0f, 0.0f,  0.0f, 0.0f,
         1.0f, -1.0f, 0.0f,  1.0f, 0.0f,
         1.0f,  1.0f, 0.0f,  1.0f, 1.0f
    };

    glGenVertexArrays(1, &_quadVAO);
    glGenBuffers(1, &_quadVBO);

    glBindVertexArray(_quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, _quadVBO);
    glBufferData(GL_ARRAY_BUFFER, quadVertices.size() * sizeof(float), quadVertices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));

    glBindVertexArray(0);

    sgct::Log::Info(std::format("Bloom effect initialized ({}x{})", width, height));
}

BloomEffect::~BloomEffect() {
    cleanupFramebuffers();

    if (_brightPassShader) glDeleteProgram(_brightPassShader);
    if (_blurShader) glDeleteProgram(_blurShader);
    if (_upsampleShader) glDeleteProgram(_upsampleShader);
    if (_compositeShader) glDeleteProgram(_compositeShader);

    if (_quadVAO) glDeleteVertexArrays(1, &_quadVAO);
    if (_quadVBO) glDeleteBuffers(1, &_quadVBO);
}

void BloomEffect::createFramebuffers(int width, int height) {
    cleanupFramebuffers();

    // Bright pass FBO (half-res, RGB16F, mip-mapped)
    _brightFBO.width = width / 2;
    _brightFBO.height = height / 2;
    glGenFramebuffers(1, &_brightFBO.fbo);
    glGenTextures(1, &_brightFBO.colorTexture);
    glBindTexture(GL_TEXTURE_2D, _brightFBO.colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, _brightFBO.width, _brightFBO.height, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);

    glBindFramebuffer(GL_FRAMEBUFFER, _brightFBO.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _brightFBO.colorTexture, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Blur FBO (half-res, RGB16F)
    _blurFBO.width = width / 2;
    _blurFBO.height = height / 2;
    glGenFramebuffers(1, &_blurFBO.fbo);
    glGenTextures(1, &_blurFBO.colorTexture);
    glBindTexture(GL_TEXTURE_2D, _blurFBO.colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, _blurFBO.width, _blurFBO.height, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, _blurFBO.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _blurFBO.colorTexture, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Upsample FBO (full-res, RGB16F)
    _upsampleFBO.width = width;
    _upsampleFBO.height = height;
    glGenFramebuffers(1, &_upsampleFBO.fbo);
    glGenTextures(1, &_upsampleFBO.colorTexture);
    glBindTexture(GL_TEXTURE_2D, _upsampleFBO.colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, _upsampleFBO.width, _upsampleFBO.height, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, _upsampleFBO.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _upsampleFBO.colorTexture, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Composite FBO (full-res, RGB8)
    _compositeFBO.width = width;
    _compositeFBO.height = height;
    glGenFramebuffers(1, &_compositeFBO.fbo);
    glGenTextures(1, &_compositeFBO.colorTexture);
    glBindTexture(GL_TEXTURE_2D, _compositeFBO.colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, _compositeFBO.width, _compositeFBO.height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, _compositeFBO.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _compositeFBO.colorTexture, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void BloomEffect::compileShaders() {
    const std::string shaderPath = "apps/simplecube/shaders/";
    const std::string vertShader = shaderPath + "bloom_fullscreen.vert";

    _brightPassShader = compileShader(vertShader, shaderPath + "bloom_bright_pass.frag");
    _blurShader = compileShader(vertShader, shaderPath + "bloom_blur.frag");
    _upsampleShader = compileShader(vertShader, shaderPath + "bloom_upsample.frag");
    _compositeShader = compileShader(vertShader, shaderPath + "bloom_composite.frag");
}

void BloomEffect::cleanupFramebuffers() {
    if (_brightFBO.fbo) {
        glDeleteFramebuffers(1, &_brightFBO.fbo);
        glDeleteTextures(1, &_brightFBO.colorTexture);
        _brightFBO = Framebuffer{};
    }
    if (_blurFBO.fbo) {
        glDeleteFramebuffers(1, &_blurFBO.fbo);
        glDeleteTextures(1, &_blurFBO.colorTexture);
        _blurFBO = Framebuffer{};
    }
    if (_upsampleFBO.fbo) {
        glDeleteFramebuffers(1, &_upsampleFBO.fbo);
        glDeleteTextures(1, &_upsampleFBO.colorTexture);
        _upsampleFBO = Framebuffer{};
    }
    if (_compositeFBO.fbo) {
        glDeleteFramebuffers(1, &_compositeFBO.fbo);
        glDeleteTextures(1, &_compositeFBO.colorTexture);
        _compositeFBO = Framebuffer{};
    }
}

void BloomEffect::resize(int width, int height) {
    if (width != _width || height != _height) {
        _width = width;
        _height = height;
        createFramebuffers(width, height);
        sgct::Log::Info(std::format("Bloom effect resized to {}x{}", width, height));
    }
}

void BloomEffect::render(GLuint inputTexture, sgct::ivec2 inputSize) {
    // Resize if needed
    if (inputSize.x != _width || inputSize.y != _height) {
        resize(inputSize.x, inputSize.y);
    }

    // Save OpenGL state
    GLint prevFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    // Pass 1: Bright pass
    renderBrightPass(inputTexture);

    // Pass 2: Generate mipmaps
    generateMips();

    // Pass 3: Blur pass
    renderBlurPass();

    // Pass 4: Upsample
    renderUpsample();

    // Pass 5: Composite
    renderComposite(inputTexture);

    // Restore state
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
}

void BloomEffect::renderBrightPass(GLuint inputTexture) {
    glBindFramebuffer(GL_FRAMEBUFFER, _brightFBO.fbo);
    glViewport(0, 0, _brightFBO.width, _brightFBO.height);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(_brightPassShader);
    glUniform1i(glGetUniformLocation(_brightPassShader, "inputTex"), 0);
    glUniform1f(glGetUniformLocation(_brightPassShader, "threshold"), _settings.threshold);
    glUniform1f(glGetUniformLocation(_brightPassShader, "softThreshold"), _settings.softThreshold);
    glUniform1f(glGetUniformLocation(_brightPassShader, "maxBrightness"), _settings.maxBrightness);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);

    glBindVertexArray(_quadVAO);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);

    glUseProgram(0);
}

void BloomEffect::generateMips() {
    glBindTexture(GL_TEXTURE_2D, _brightFBO.colorTexture);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void BloomEffect::renderBlurPass() {
    glBindFramebuffer(GL_FRAMEBUFFER, _blurFBO.fbo);
    glViewport(0, 0, _blurFBO.width, _blurFBO.height);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(_blurShader);
    glUniform1i(glGetUniformLocation(_blurShader, "brightTex"), 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _brightFBO.colorTexture);

    glBindVertexArray(_quadVAO);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);

    glUseProgram(0);
}

void BloomEffect::renderUpsample() {
    glBindFramebuffer(GL_FRAMEBUFFER, _upsampleFBO.fbo);
    glViewport(0, 0, _upsampleFBO.width, _upsampleFBO.height);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(_upsampleShader);
    glUniform1i(glGetUniformLocation(_upsampleShader, "bloomTex"), 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _blurFBO.colorTexture);

    glBindVertexArray(_quadVAO);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);

    glUseProgram(0);
}

void BloomEffect::renderComposite(GLuint inputTexture) {
    glBindFramebuffer(GL_FRAMEBUFFER, _compositeFBO.fbo);
    glViewport(0, 0, _compositeFBO.width, _compositeFBO.height);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(_compositeShader);
    glUniform1i(glGetUniformLocation(_compositeShader, "sceneTex"), 0);
    glUniform1i(glGetUniformLocation(_compositeShader, "bloomTex"), 1);
    glUniform1f(glGetUniformLocation(_compositeShader, "bloomStrength"), _settings.bloomStrength);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, _upsampleFBO.colorTexture);

    glBindVertexArray(_quadVAO);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);

    glActiveTexture(GL_TEXTURE0);
    glUseProgram(0);
}

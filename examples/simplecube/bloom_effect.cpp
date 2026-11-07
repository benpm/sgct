/*****************************************************************************************
 * SGCT                                                                                  *
 * Simple Graphics Cluster Toolkit                                                       *
 *                                                                                       *
 * Copyright (c) 2012-2026                                                               *
 * For conditions of distribution and use, see copyright notice in LICENSE.md            *
 ****************************************************************************************/

#include "bloom_effect.h"

#include <sgct/log.h>
#include <algorithm>
#include <array>
#include <format>
#include <string_view>

namespace {
    constexpr std::string_view FullscreenVert = R"(
  #version 330 core

  layout(location = 0) in vec3 in_position;
  layout(location = 1) in vec2 in_texCoords;

  out vec2 uv;

  void main() {
    gl_Position = vec4(in_position, 1.0);
    uv = in_texCoords;
  }
)";

    constexpr std::string_view BrightPassFrag = R"(
  #version 330 core

  uniform sampler2D inputTex;
  uniform float threshold;
  uniform float softThreshold;
  uniform float maxBrightness;

  in vec2 uv;
  out vec4 fragColor;

  void main() {
    vec3 color = texture(inputTex, uv).rgb;
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));

    // Soft threshold so that the bloom fades in instead of popping
    float contribution = smoothstep(
      threshold - softThreshold,
      threshold + softThreshold,
      luminance
    );

    // Clamp to prevent single very bright pixels from dominating the blur
    vec3 bright = min(color * contribution, vec3(maxBrightness));
    fragColor = vec4(bright, 1.0);
  }
)";

    constexpr std::string_view BlurFrag = R"(
  #version 330 core

  uniform sampler2D brightTex;
  uniform int mipCount;

  in vec2 uv;
  out vec4 fragColor;

  void main() {
    // Accumulating successively smaller mip levels approximates a wide Gaussian blur
    const float weights[10] = float[](
      0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216,
      0.0027027, 0.001, 0.0005, 0.0002, 0.0001
    );

    vec3 bloom = vec3(0.0);
    float totalWeight = 0.0;
    int iterations = min(mipCount, 10);
    for (int i = 0; i < iterations; i++) {
      bloom += textureLod(brightTex, uv, float(i)).rgb * weights[i];
      totalWeight += weights[i];
    }

    fragColor = vec4(bloom / totalWeight, 1.0);
  }
)";

    constexpr std::string_view UpsampleFrag = R"(
  #version 330 core

  uniform sampler2D bloomTex;

  in vec2 uv;
  out vec4 fragColor;

  void main() {
    // 3x3 tent filter for a smooth upsample
    vec2 texelSize = 1.0 / vec2(textureSize(bloomTex, 0));

    vec3 tent = vec3(0.0);
    tent += texture(bloomTex, uv + vec2(-1, -1) * texelSize).rgb * 0.0625;
    tent += texture(bloomTex, uv + vec2( 0, -1) * texelSize).rgb * 0.125;
    tent += texture(bloomTex, uv + vec2( 1, -1) * texelSize).rgb * 0.0625;
    tent += texture(bloomTex, uv + vec2(-1,  0) * texelSize).rgb * 0.125;
    tent += texture(bloomTex, uv + vec2( 0,  0) * texelSize).rgb * 0.25;
    tent += texture(bloomTex, uv + vec2( 1,  0) * texelSize).rgb * 0.125;
    tent += texture(bloomTex, uv + vec2(-1,  1) * texelSize).rgb * 0.0625;
    tent += texture(bloomTex, uv + vec2( 0,  1) * texelSize).rgb * 0.125;
    tent += texture(bloomTex, uv + vec2( 1,  1) * texelSize).rgb * 0.0625;

    fragColor = vec4(tent, 1.0);
  }
)";

    constexpr std::string_view CompositeFrag = R"(
  #version 330 core

  uniform sampler2D sceneTex;
  uniform sampler2D bloomTex;
  uniform float bloomStrength;
  uniform float exposure;
  uniform float gamma;

  in vec2 uv;
  out vec4 fragColor;

  // ACES filmic tone mapping curve fit by Krzysztof Narkowicz
  vec3 acesFilm(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
  }

  void main() {
    vec3 scene = texture(sceneTex, uv).rgb;
    vec3 bloom = texture(bloomTex, uv).rgb;

    vec3 combined = (scene + bloom * bloomStrength) * exposure;
    vec3 tonemapped = acesFilm(combined);
    fragColor = vec4(pow(tonemapped, vec3(1.0 / gamma)), 1.0);
  }
)";

    GLuint compileShader(GLenum type, std::string_view source, std::string_view name) {
        const GLuint shader = glCreateShader(type);
        const char* src = source.data();
        const GLint length = static_cast<GLint>(source.size());
        glShaderSource(shader, 1, &src, &length);
        glCompileShader(shader);

        GLint success = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            std::array<GLchar, 1024> infoLog = {};
            glGetShaderInfoLog(shader, 1024, nullptr, infoLog.data());
            sgct::Log::Error(std::format(
                "Bloom shader '{}' failed to compile: {}", name, infoLog.data()
            ));
        }
        return shader;
    }

    GLuint createProgram(std::string_view fragmentSource, std::string_view name) {
        const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, FullscreenVert, name);
        const GLuint fragmentShader =
            compileShader(GL_FRAGMENT_SHADER, fragmentSource, name);

        const GLuint program = glCreateProgram();
        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);
        glLinkProgram(program);

        GLint success = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            std::array<GLchar, 1024> infoLog = {};
            glGetProgramInfoLog(program, 1024, nullptr, infoLog.data());
            sgct::Log::Error(std::format(
                "Bloom shader '{}' failed to link: {}", name, infoLog.data()
            ));
        }

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return program;
    }

    void createFramebuffer(GLuint& fbo, GLuint& texture, int width, int height,
                           GLenum internalFormat, GLenum format, GLenum type,
                           bool mipmapped)
    {
        glGenFramebuffers(1, &fbo);
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            static_cast<GLint>(internalFormat),
            width,
            height,
            0,
            format,
            type,
            nullptr
        );
        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_MIN_FILTER,
            mipmapped ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR
        );
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (mipmapped) {
            glGenerateMipmap(GL_TEXTURE_2D);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(
            GL_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D,
            texture,
            0
        );
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
} // namespace

BloomEffect::BloomEffect(int width, int height) {
    createFramebuffers(width, height);
    compileShaders();

    // Full-screen quad with positions and texture coordinates
    constexpr std::array<float, 20> QuadVertices = {
        -1.f,  1.f, 0.f,  0.f, 1.f,
        -1.f, -1.f, 0.f,  0.f, 0.f,
         1.f, -1.f, 0.f,  1.f, 0.f,
         1.f,  1.f, 0.f,  1.f, 1.f
    };

    glGenVertexArrays(1, &_quadVAO);
    glGenBuffers(1, &_quadVBO);
    glBindVertexArray(_quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, _quadVBO);
    glBufferData(
        GL_ARRAY_BUFFER,
        QuadVertices.size() * sizeof(float),
        QuadVertices.data(),
        GL_STATIC_DRAW
    );
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        2,
        GL_FLOAT,
        GL_FALSE,
        5 * sizeof(float),
        reinterpret_cast<void*>(3 * sizeof(float))
    );
    glBindVertexArray(0);

    sgct::Log::Info(std::format("Bloom effect initialized ({}x{})", width, height));
}

BloomEffect::~BloomEffect() {
    destroyFramebuffers();

    glDeleteProgram(_brightPassShader);
    glDeleteProgram(_blurShader);
    glDeleteProgram(_upsampleShader);
    glDeleteProgram(_compositeShader);

    glDeleteVertexArrays(1, &_quadVAO);
    glDeleteBuffers(1, &_quadVBO);
}

void BloomEffect::createFramebuffers(int width, int height) {
    destroyFramebuffers();
    _width = width;
    _height = height;

    const int halfWidth = std::max(width / 2, 1);
    const int halfHeight = std::max(height / 2, 1);

    _brightFBO.width = halfWidth;
    _brightFBO.height = halfHeight;
    createFramebuffer(
        _brightFBO.fbo, _brightFBO.colorTexture,
        halfWidth, halfHeight, GL_RGB16F, GL_RGB, GL_FLOAT, true
    );

    _blurFBO.width = halfWidth;
    _blurFBO.height = halfHeight;
    createFramebuffer(
        _blurFBO.fbo, _blurFBO.colorTexture,
        halfWidth, halfHeight, GL_RGB16F, GL_RGB, GL_FLOAT, false
    );

    _upsampleFBO.width = width;
    _upsampleFBO.height = height;
    createFramebuffer(
        _upsampleFBO.fbo, _upsampleFBO.colorTexture,
        width, height, GL_RGB16F, GL_RGB, GL_FLOAT, false
    );

    _compositeFBO.width = width;
    _compositeFBO.height = height;
    createFramebuffer(
        _compositeFBO.fbo, _compositeFBO.colorTexture,
        width, height, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, false
    );
}

void BloomEffect::destroyFramebuffers() {
    for (Framebuffer* fb : { &_brightFBO, &_blurFBO, &_upsampleFBO, &_compositeFBO }) {
        glDeleteFramebuffers(1, &fb->fbo);
        glDeleteTextures(1, &fb->colorTexture);
        *fb = Framebuffer();
    }
}

void BloomEffect::compileShaders() {
    _brightPassShader = createProgram(BrightPassFrag, "bright pass");
    _blurShader = createProgram(BlurFrag, "blur");
    _upsampleShader = createProgram(UpsampleFrag, "upsample");
    _compositeShader = createProgram(CompositeFrag, "composite");
}

void BloomEffect::drawQuad() const {
    glBindVertexArray(_quadVAO);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);
}

void BloomEffect::render(GLuint inputTexture, glm::ivec2 inputSize) {
    if (inputSize.x != _width || inputSize.y != _height) {
        createFramebuffers(inputSize.x, inputSize.y);
        sgct::Log::Info(std::format("Bloom effect resized to {}x{}", _width, _height));
    }

    // The caller (the SGCT post-process callback) has a framebuffer and viewport set up
    // that the result has to be drawn into, so both are restored afterwards
    GLint prevFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    std::array<GLint, 4> prevViewport = {};
    glGetIntegerv(GL_VIEWPORT, prevViewport.data());

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    renderBrightPass(inputTexture);
    glBindTexture(GL_TEXTURE_2D, _brightFBO.colorTexture);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    renderBlurPass();
    renderUpsample();
    renderComposite(inputTexture);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFBO));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
}

void BloomEffect::renderBrightPass(GLuint inputTexture) {
    glBindFramebuffer(GL_FRAMEBUFFER, _brightFBO.fbo);
    glViewport(0, 0, _brightFBO.width, _brightFBO.height);

    glUseProgram(_brightPassShader);
    glUniform1i(glGetUniformLocation(_brightPassShader, "inputTex"), 0);
    glUniform1f(
        glGetUniformLocation(_brightPassShader, "threshold"),
        _settings.threshold
    );
    glUniform1f(
        glGetUniformLocation(_brightPassShader, "softThreshold"),
        _settings.softThreshold
    );
    glUniform1f(
        glGetUniformLocation(_brightPassShader, "maxBrightness"),
        _settings.maxBrightness
    );

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    drawQuad();
    glUseProgram(0);
}

void BloomEffect::renderBlurPass() {
    glBindFramebuffer(GL_FRAMEBUFFER, _blurFBO.fbo);
    glViewport(0, 0, _blurFBO.width, _blurFBO.height);

    glUseProgram(_blurShader);
    glUniform1i(glGetUniformLocation(_blurShader, "brightTex"), 0);
    glUniform1i(glGetUniformLocation(_blurShader, "mipCount"), _settings.mipLevels);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _brightFBO.colorTexture);
    drawQuad();
    glUseProgram(0);
}

void BloomEffect::renderUpsample() {
    glBindFramebuffer(GL_FRAMEBUFFER, _upsampleFBO.fbo);
    glViewport(0, 0, _upsampleFBO.width, _upsampleFBO.height);

    glUseProgram(_upsampleShader);
    glUniform1i(glGetUniformLocation(_upsampleShader, "bloomTex"), 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _blurFBO.colorTexture);
    drawQuad();
    glUseProgram(0);
}

void BloomEffect::renderComposite(GLuint inputTexture) {
    glBindFramebuffer(GL_FRAMEBUFFER, _compositeFBO.fbo);
    glViewport(0, 0, _compositeFBO.width, _compositeFBO.height);

    glUseProgram(_compositeShader);
    glUniform1i(glGetUniformLocation(_compositeShader, "sceneTex"), 0);
    glUniform1i(glGetUniformLocation(_compositeShader, "bloomTex"), 1);
    glUniform1f(
        glGetUniformLocation(_compositeShader, "bloomStrength"),
        _settings.bloomStrength
    );
    glUniform1f(glGetUniformLocation(_compositeShader, "exposure"), _settings.exposure);
    glUniform1f(glGetUniformLocation(_compositeShader, "gamma"), _settings.gamma);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, _upsampleFBO.colorTexture);
    drawQuad();
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(0);
}

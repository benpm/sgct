/*****************************************************************************************
 * SGCT                                                                                  *
 * Simple Graphics Cluster Toolkit                                                       *
 *                                                                                       *
 * Copyright (c) 2012-2026                                                               *
 * For conditions of distribution and use, see copyright notice in LICENSE.md            *
 ****************************************************************************************/

#ifndef __SIMPLECUBE__BLOOM_EFFECT__H__
#define __SIMPLECUBE__BLOOM_EFFECT__H__

#include <sgct/glm.h>
#include <sgct/opengl.h>

/**
 * A fast bloom effect that approximates a wide Gaussian blur by sampling the mip chain of
 * a half-resolution bright-pass texture. It is meant to be driven from the SGCT
 * post-process callback: `render` takes the texture that SGCT hands to the callback and
 * produces a tone-mapped composite in `outputTexture`, which the callback then draws into
 * the framebuffer SGCT has bound.
 */
class BloomEffect {
public:
    struct Settings {
        /// Luminance above which pixels contribute to the bloom
        float threshold = 1.f;
        /// Width of the smoothstep around the threshold
        float softThreshold = 0.5f;
        /// Weight of the bloom in the final composite
        float bloomStrength = 0.04f;
        /// Clamp for the bright pass to avoid fireflies
        float maxBrightness = 10.f;
        /// Number of mip levels that are accumulated in the blur pass (at most 10)
        int mipLevels = 6;
        /// Gamma that the composite is encoded with
        float gamma = 2.2f;
        /// Exposure that is applied before tone mapping
        float exposure = 1.f;
    };

    BloomEffect(int width, int height);
    ~BloomEffect();

    BloomEffect(const BloomEffect&) = delete;
    BloomEffect& operator=(const BloomEffect&) = delete;

    /// Runs all bloom passes on \p inputTexture, resizing the internal buffers if needed
    void render(GLuint inputTexture, glm::ivec2 inputSize);

    /// The final composite of scene and bloom
    GLuint outputTexture() const { return _compositeFBO.colorTexture; }

    // Intermediate textures for debugging the individual passes
    GLuint brightPassTexture() const { return _brightFBO.colorTexture; }
    GLuint blurTexture() const { return _blurFBO.colorTexture; }
    GLuint upsampleTexture() const { return _upsampleFBO.colorTexture; }

    Settings& settings() { return _settings; }
    const Settings& settings() const { return _settings; }

private:
    struct Framebuffer {
        GLuint fbo = 0;
        GLuint colorTexture = 0;
        int width = 0;
        int height = 0;
    };

    void createFramebuffers(int width, int height);
    void destroyFramebuffers();
    void compileShaders();
    void drawQuad() const;

    void renderBrightPass(GLuint inputTexture);
    void renderBlurPass();
    void renderUpsample();
    void renderComposite(GLuint inputTexture);

    Settings _settings;
    int _width = 0;
    int _height = 0;

    Framebuffer _brightFBO;     // Half resolution, RGB16F, with a mip chain
    Framebuffer _blurFBO;       // Half resolution, RGB16F
    Framebuffer _upsampleFBO;   // Full resolution, RGB16F
    Framebuffer _compositeFBO;  // Full resolution, RGBA8

    GLuint _brightPassShader = 0;
    GLuint _blurShader = 0;
    GLuint _upsampleShader = 0;
    GLuint _compositeShader = 0;

    GLuint _quadVAO = 0;
    GLuint _quadVBO = 0;
};

#endif // __SIMPLECUBE__BLOOM_EFFECT__H__

/*****************************************************************************************
 * SGCT                                                                                  *
 * Simple Graphics Cluster Toolkit                                                       *
 *                                                                                       *
 * Copyright (c) 2012-2025                                                               *
 * For conditions of distribution and use, see copyright notice in LICENSE.md            *
 ****************************************************************************************/

#ifndef __SIMPLECUBE__BLOOM_EFFECT__H__
#define __SIMPLECUBE__BLOOM_EFFECT__H__

#include <sgct/opengl.h>
#include <sgct/math.h>

class BloomEffect {
public:
    struct Settings {
        float threshold = 1.0f;
        float softThreshold = 0.5f;
        float bloomStrength = 0.04f;
        float maxBrightness = 10.0f;
        int mipLevels = 6;
        bool useTentFilter = true;
        float gamma = 2.2f;
        float exposure = 1.0f;
    };

    BloomEffect(int width, int height);
    ~BloomEffect();

    // Render bloom effect
    void render(GLuint inputTexture, sgct::ivec2 inputSize);

    // Get final output texture (to be rendered to screen)
    GLuint outputTexture() const { return _compositeFBO.colorTexture; }

    // Debug textures for visualization
    GLuint brightPassTexture() const { return _brightFBO.colorTexture; }
    GLuint blurTexture() const { return _blurFBO.colorTexture; }
    GLuint upsampleTexture() const { return _upsampleFBO.colorTexture; }

    // Runtime parameter adjustment
    void setSettings(const Settings& settings) { _settings = settings; }
    Settings& settings() { return _settings; }
    const Settings& settings() const { return _settings; }

    // Resize (when window changes)
    void resize(int width, int height);

private:
    void createFramebuffers(int width, int height);
    void compileShaders();
    void renderBrightPass(GLuint inputTexture);
    void generateMips();
    void renderBlurPass();
    void renderUpsample();
    void renderComposite(GLuint inputTexture);
    void cleanupFramebuffers();

    struct Framebuffer {
        GLuint fbo = 0;
        GLuint colorTexture = 0;
        int width = 0;
        int height = 0;
    };

    Settings _settings;

    // Framebuffers
    Framebuffer _brightFBO;      // Half-res, RGB16F, mip-mapped
    Framebuffer _blurFBO;        // Half-res, RGB16F
    Framebuffer _upsampleFBO;    // Full-res, RGB16F
    Framebuffer _compositeFBO;   // Full-res, RGB8

    // Shaders
    GLuint _brightPassShader = 0;
    GLuint _blurShader = 0;
    GLuint _upsampleShader = 0;
    GLuint _compositeShader = 0;

    // Quad VAO for full-screen rendering
    GLuint _quadVAO = 0;
    GLuint _quadVBO = 0;

    int _width = 0;
    int _height = 0;
};

#endif // __SIMPLECUBE__BLOOM_EFFECT__H__

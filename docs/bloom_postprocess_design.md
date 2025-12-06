# Fast Bloom Post-Processing Design Document

**Target Application:** SGCT simplecube testbed
**Technique:** Mip-map Gaussian Approximation Bloom
**Color Space:** HDR (High Dynamic Range)
**Priority:** Performance-optimized with acceptable visual quality
**Date:** 2025-12-05

## Overview

This document describes the implementation of a fast, high-quality bloom post-processing effect for SGCT using mip-map based Gaussian blur approximation. The technique leverages hardware mip-map generation and bilinear filtering to achieve performant bloom while maintaining visual fidelity.

### Goals

1. **Performance**: Minimize GPU cost while achieving visible bloom effect
2. **Quality**: Smooth, natural-looking glow on bright areas
3. **HDR-Aware**: Properly handle high dynamic range color values
4. **Scalability**: Adjustable quality/performance trade-offs
5. **Integration**: Clean integration with SGCT's post-processing pipeline

### Non-Goals

- Physically accurate light scattering simulation
- Lens flare/starburst effects (future enhancement)
- Temporal accumulation/motion blur
- Adaptive/automatic threshold adjustment

## Technical Approach

### Algorithm Overview

**Multi-pass bloom using mip-map Gaussian approximation:**

1. **Bright Pass** - Extract bright pixels above threshold (HDR-aware)
2. **Downsampling Chain** - Generate mip-chain with box filtering
3. **Gaussian Approximation** - Sample multiple mip levels to approximate blur
4. **Upsampling** - Reconstruct full-resolution bloom texture
5. **Composite** - Blend bloom with original scene (tone-mapped)

### Why Mip-Map Gaussian Approximation?

Traditional Gaussian blur requires many texture samples per pixel (e.g., 13×13 = 169 samples for good quality). The mip-map technique:

- **Reduces samples**: ~6-8 samples total vs. 100+ for separable Gaussian
- **Leverages hardware**: GPU mip-map generation is highly optimized
- **Approximates well**: Averaging progressively larger areas mimics Gaussian falloff
- **Scales well**: Cost is resolution-independent for the blur phase

**Trade-off**: Slightly blockier blur compared to true Gaussian, but acceptable for bloom where glow is the primary goal.

## Implementation Design

### Architecture

```
Input Texture (HDR RGB16F/RGB32F)
         ↓
   [Bright Pass]  ← Threshold extraction, HDR → SDR mapping
         ↓
  Bright Texture (RGB16F, 1/2 res)
         ↓
  [Generate Mips] ← Hardware mip-map generation (glGenerateMipmap)
         ↓
  Mip Chain (5-7 levels)
         ↓
  [Gaussian Sample] ← Weighted multi-level sampling
         ↓
   Bloom Texture
         ↓
  [Upsample] ← Bilinear upsample to full res
         ↓
  Full-res Bloom
         ↓
  [Composite] ← Additive blend with original + tone mapping
         ↓
    Final Output
```

### Render Passes

#### Pass 1: Bright Pass (Downsampled)

**Purpose**: Extract bright pixels and downsample to reduce resolution for blur operations.

**Framebuffer**: RGB16F, half input resolution (inputRes / 2)

**Vertex Shader**: Full-screen quad (standard)

**Fragment Shader Logic**:
```glsl
// Sample input at current UV
vec3 color = texture(inputTex, uv).rgb;

// HDR luminance extraction
float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));

// Threshold with smooth falloff (soft threshold)
float threshold = 1.0; // Configurable
float softness = 0.5;  // Configurable
float contribution = smoothstep(threshold - softness, threshold + softness, luminance);

// Extract bright color (preserve HDR values)
vec3 bright = color * contribution;

// Optional: Clamp maximum brightness to prevent fireflies
bright = min(bright, vec3(10.0)); // Configurable max

fragColor = vec4(bright, 1.0);
```

**Key Parameters**:
- `threshold`: Luminance threshold for bloom (default: 1.0)
- `softness`: Smoothness of threshold transition (default: 0.5)
- `maxBrightness`: Clamp to prevent extreme values (default: 10.0)

#### Pass 2: Mip-Map Generation

**Purpose**: Generate mip-map chain using hardware filtering.

**Implementation**: Call `glGenerateMipmap(GL_TEXTURE_2D)` on bright pass output.

**Mip Levels**: Auto-generate down to 1×1 or stop at reasonable size (e.g., 4×4).

**Cost**: Highly optimized GPU operation, ~1-2ms for 1080p half-res texture.

#### Pass 3: Gaussian Approximation Sampling

**Purpose**: Sample multiple mip levels to approximate Gaussian blur.

**Framebuffer**: RGB16F, same size as bright pass (half res)

**Fragment Shader Logic**:
```glsl
// Sample 6 mip levels with Gaussian-like weights
vec3 bloom = vec3(0.0);
float totalWeight = 0.0;

// Weights approximate Gaussian distribution
// Center (level 0) has highest weight, decreases for higher levels
float weights[6] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216, 0.0027027);

for (int i = 0; i < 6; i++) {
    vec3 sample = textureLod(brightTex, uv, float(i)).rgb;
    bloom += sample * weights[i];
    totalWeight += weights[i];
}

bloom /= totalWeight; // Normalize

fragColor = vec4(bloom, 1.0);
```

**Optimization**: Weights are pre-normalized, can be baked into shader constants.

**Alternative**: Use fixed 3-4 level sampling for more performance at cost of quality.

#### Pass 4: Upsample to Full Resolution

**Purpose**: Reconstruct full-resolution bloom texture with bilinear filtering.

**Framebuffer**: RGB16F, full input resolution

**Fragment Shader Logic**:
```glsl
// Simple bilinear upsample (hardware filtered)
vec3 bloom = texture(bloomTex, uv).rgb;

// Optional: Apply gentle additional blur during upsample
// (tent filter via 4-tap sampling)
vec2 texelSize = 1.0 / textureSize(bloomTex, 0);
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

bloom = tent; // Use tent filter result

fragColor = vec4(bloom, 1.0);
```

**Note**: Tent filter is optional - adds ~8 samples but smooths the upsampling.

#### Pass 5: Final Composite

**Purpose**: Combine bloom with original scene and apply tone mapping.

**Framebuffer**: Default framebuffer (or window's render target)

**Fragment Shader Logic**:
```glsl
// Sample original scene (HDR)
vec3 scene = texture(sceneTex, uv).rgb;

// Sample bloom
vec3 bloom = texture(bloomTex, uv).rgb;

// Bloom intensity control
float bloomStrength = 0.04; // Configurable (0.0 - 1.0)
vec3 combined = scene + bloom * bloomStrength;

// Tone mapping (ACES filmic approximation)
vec3 tonemapped = ACESFilm(combined);

// Gamma correction
tonemapped = pow(tonemapped, vec3(1.0 / 2.2));

fragColor = vec4(tonemapped, 1.0);
```

**ACES Tone Mapping Function**:
```glsl
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
```

**Alternative Tone Mappers**:
- **Reinhard**: `x / (1 + x)` (simpler, less saturated)
- **Uncharted 2**: More complex, better highlight preservation
- **None**: If rendering to HDR display

### HDR Color Encoding

**Texture Formats**:
- **RGB16F** (preferred): Half-float, 16-bit per channel
  - Range: ±65504
  - Precision: Good for most HDR content
  - Memory: 6 bytes per pixel
  - Performance: Excellent on modern GPUs

- **RGB32F** (alternative): Full float, 32-bit per channel
  - Range: Full IEEE float
  - Precision: Overkill for most cases
  - Memory: 12 bytes per pixel
  - Performance: ~2× bandwidth cost vs RGB16F

**Recommendation**: Use RGB16F for bloom pipeline, only upgrade to RGB32F if precision issues occur.

**Color Space**: Linear RGB (no gamma encoding until final output)

## SGCT Integration

### Modified simplecube Structure

**New Files**:
- `apps/simplecube/bloom_shaders.h` - Shader source code
- `apps/simplecube/bloom_effect.h` - Bloom effect class declaration
- `apps/simplecube/bloom_effect.cpp` - Bloom effect implementation
- `config/single_bloom.json` - Example config with HDR enabled

**Modified Files**:
- `apps/simplecube/main.cpp` - Initialize bloom, update postProcess callback
- `apps/simplecube/CMakeLists.txt` - Add bloom source files to build

### Bloom Effect Class Design

```cpp
class BloomEffect {
public:
    struct Settings {
        float threshold = 1.0f;
        float softThreshold = 0.5f;
        float bloomStrength = 0.04f;
        float maxBrightness = 10.0f;
        int mipLevels = 6;
        bool useTentFilter = true;
    };

    BloomEffect(int width, int height);
    ~BloomEffect();

    // Render bloom effect
    void render(GLuint inputTexture, ivec2 inputSize);

    // Get final output texture (to be rendered to screen)
    GLuint outputTexture() const { return _compositeFBO.colorTexture; }

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

    struct Framebuffer {
        GLuint fbo;
        GLuint colorTexture;
        int width;
        int height;
    };

    Settings _settings;

    // Framebuffers
    Framebuffer _brightFBO;      // Half-res, RGB16F, mip-mapped
    Framebuffer _blurFBO;        // Half-res, RGB16F
    Framebuffer _upsampleFBO;    // Full-res, RGB16F
    Framebuffer _compositeFBO;   // Full-res, RGB8/RGB16F

    // Shaders
    GLuint _brightPassShader;
    GLuint _blurShader;
    GLuint _upsampleShader;
    GLuint _compositeShader;

    // Quad VAO for full-screen rendering
    GLuint _quadVAO;
    GLuint _quadVBO;
};
```

### Integration into main.cpp

```cpp
namespace {
    std::unique_ptr<BloomEffect> bloomEffect;
    bool enableBloom = true; // Toggle with keyboard
}

void initOGL(GLFWwindow*) {
    // ... existing initialization ...

    // Initialize bloom effect (use window size)
    // Note: Window must be configured with "bufferBitDepth": "16float" for proper HDR
    const ivec2 windowSize = Engine::instance().thisNode().windows()[0]->framebufferSize();
    bloomEffect = std::make_unique<BloomEffect>(windowSize.x, windowSize.y);

    Log::Info("Bloom effect initialized (%dx%d)", windowSize.x, windowSize.y);
}

void postProcess(const Window& window, FrustumMode, unsigned int inputTexture, ivec2 size) {
    if (enableBloom && bloomEffect) {
        // Render bloom effect (internally composites with original scene)
        bloomEffect->render(inputTexture, size);

        // Bloom effect outputs composited result, blit to current framebuffer
        GLuint bloomOutput = bloomEffect->outputTexture();

        // Render full-screen quad with bloom output
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, bloomOutput);
        window.renderScreenQuad();
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

void keyboard(Key key, Modifier, Action action, int, Window*) {
    if (action == Action::Press) {
        if (key == Key::Esc) {
            Engine::instance().terminate();
        } else if (key == Key::B) {
            enableBloom = !enableBloom;
            Log::Info("Bloom: %s", enableBloom ? "ON" : "OFF");
        } else if (key == Key::Up && bloomEffect) {
            auto& settings = bloomEffect->settings();
            settings.bloomStrength = std::min(settings.bloomStrength + 0.01f, 1.0f);
            Log::Info("Bloom strength: %.3f", settings.bloomStrength);
        } else if (key == Key::Down && bloomEffect) {
            auto& settings = bloomEffect->settings();
            settings.bloomStrength = std::max(settings.bloomStrength - 0.01f, 0.0f);
            Log::Info("Bloom strength: %.3f", settings.bloomStrength);
        } else if (key == Key::T && bloomEffect) {
            auto& settings = bloomEffect->settings();
            settings.threshold += 0.1f;
            Log::Info("Bloom threshold: %.2f", settings.threshold);
        } else if (key == Key::G && bloomEffect) {
            auto& settings = bloomEffect->settings();
            settings.threshold = std::max(settings.threshold - 0.1f, 0.0f);
            Log::Info("Bloom threshold: %.2f", settings.threshold);
        }
    }
}
```

### Required Changes to SGCT

**HDR Support**: SGCT already supports HDR rendering through the `bufferBitDepth` configuration option.

**Window Configuration Options** (`config::Window::ColorBitDepth`):
- `Depth8` - GL_RGBA8 (default, LDR)
- `Depth16` - GL_RGBA16 (high precision LDR)
- `Depth16Float` - GL_RGBA16F (HDR, recommended for bloom)
- `Depth16Int` - GL_RGBA16I (integer)
- `Depth32Float` - GL_RGBA32F (full precision HDR, overkill)

**For Bloom Implementation**:
1. **Recommended**: Add `"bufferBitDepth": "16float"` to config JSON for HDR support
2. **Alternative**: Detect buffer format and adapt (support both LDR and HDR inputs)
3. The `postProcess` callback receives the texture in the format specified by `bufferBitDepth`

**Example HDR Configuration Addition**:
```json
{
  "version": 1,
  "masteraddress": "localhost",
  "nodes": [{
    "address": "localhost",
    "port": 20401,
    "windows": [{
      "size": { "x": 1280, "y": 720 },
      "bufferBitDepth": "16float",  // Enable HDR
      "viewports": [...]
    }]
  }]
}
```

**Fallback Strategy** (if user doesn't enable HDR):
- Detect format in bloom initialization
- If LDR (RGBA8), adjust threshold to work with clamped [0,1] range
- Still produce bloom effect, just less dramatic on bright values

## Performance Analysis

### Expected Costs (1920×1080, RGB16F)

| Pass | Resolution | Samples/Pixel | Cost (approx) |
|------|-----------|---------------|---------------|
| Bright Pass | 960×540 | 1 | 0.1ms |
| Mip Generation | 960×540→1×1 | - | 0.2ms |
| Blur Pass | 960×540 | 6 | 0.2ms |
| Upsample | 1920×1080 | 1-9 | 0.3ms |
| Composite | 1920×1080 | 2 | 0.2ms |
| **Total** | - | - | **~1.0ms** |

**Target**: < 2ms at 1080p on mid-range GPU (GTX 1060 / RX 580 or better)

**Scaling**:
- 4K (3840×2160): ~3-4ms expected
- 720p (1280×720): ~0.5ms expected

### Optimization Opportunities

1. **Reduce Mip Levels**: Use 4 instead of 6 (faster, slightly blockier)
2. **Skip Tent Filter**: Remove upsample tent filter (saves 8 samples)
3. **Lower Precision**: Use R11G11B10F instead of RGB16F (saves bandwidth)
4. **Combined Passes**: Merge upsample + composite into single pass
5. **Async Compute**: Run bloom on compute queue (advanced, requires more work)

### Quality Presets

```cpp
// Low - fastest, acceptable quality
Settings low = {
    .threshold = 1.2f,
    .bloomStrength = 0.03f,
    .mipLevels = 4,
    .useTentFilter = false
};

// Medium - balanced (default)
Settings medium = {
    .threshold = 1.0f,
    .bloomStrength = 0.04f,
    .mipLevels = 6,
    .useTentFilter = true
};

// High - best quality, slower
Settings high = {
    .threshold = 0.8f,
    .bloomStrength = 0.05f,
    .mipLevels = 8,
    .useTentFilter = true
};
```

## Visual Quality Considerations

### Strengths

- **Natural Glow**: Multi-mip sampling produces smooth, believable bloom
- **HDR Aware**: Properly handles bright values > 1.0
- **Adjustable**: Easy to tweak threshold and strength for different scenes
- **Stable**: No flickering or temporal artifacts

### Weaknesses

- **Slightly Blocky**: Not as smooth as true Gaussian blur
- **No Directionality**: Uniform blur (no lens effects)
- **Fixed Falloff**: Can't customize blur kernel shape

### Visual Validation

Test with simplecube's colored boxes:
1. **Bright boxes should glow** - Check threshold is working
2. **Glow should be smooth** - Check mip sampling weights
3. **Colors should blend** - Adjacent bright boxes should create color mixing
4. **No fireflies** - Max brightness clamp should prevent sparkling
5. **Scene should pop** - Tone mapping should enhance contrast

## Testing Plan

### Unit Tests

1. **Framebuffer Creation**: Verify correct sizes and formats
2. **Shader Compilation**: All shaders compile without errors
3. **Texture Sampling**: Verify mip levels are generated correctly
4. **Parameter Ranges**: Test edge cases (strength=0, strength=1, etc.)

### Integration Tests

1. **Performance**: Measure frame time with bloom on/off
2. **Memory**: Check for leaks after enable/disable cycles
3. **Resize**: Verify bloom works after window resize
4. **Multi-window**: Test with multiple SGCT windows
5. **Projections**: Verify bloom works with fisheye, cylindrical, etc.

### Visual Tests

1. Run simplecube with various configs
2. Adjust bloom strength with keyboard (↑/↓)
3. Toggle bloom on/off (B key)
4. Test with bright white boxes vs. dark scene
5. Test with different projection types
6. Compare with reference screenshots

## Future Enhancements

### Phase 2 (Post-Initial Implementation)

1. **Lens Dirt**: Multiply bloom by dirt texture for realism
2. **Chromatic Aberration**: Separate RGB channels for color fringing
3. **Anamorphic Bloom**: Horizontal streak for cinematic look
4. **Adaptive Threshold**: Auto-adjust based on scene luminance
5. **Temporal Filtering**: Smooth bloom over multiple frames

### Phase 3 (Advanced)

1. **Physical Bloom**: Use actual point spread function
2. **Bokeh Shapes**: Custom blur kernel shapes (hexagon, etc.)
3. **Lens Flares**: Add starburst and ghost effects
4. **GPU Compute**: Move to compute shaders for better performance

## References

### Academic Papers

- **"Next Generation Post Processing in Call of Duty: Advanced Warfare"** (Jimenez, 2014)
  - Mip-map based bloom technique origin

- **"Bandwidth-Efficient Graphics"** (Bjorke, 2004)
  - Half-resolution rendering techniques

### Industry Resources

- **Unity Post-Processing Stack V2** - Similar mip-map bloom implementation
- **Unreal Engine Bloom Documentation** - HDR bloom best practices
- **GPU Gems 3, Chapter 40** - Incremental improvements to bloom

### SGCT Specific

- `include/sgct/engine.h` - Engine::Callbacks structure and postProcess signature
- `include/sgct/callbackdata.h` - RenderData structure (not used in postProcess)
- `include/sgct/window.h` - Window::renderScreenQuad() helper
- `include/sgct/config.h` - Window::ColorBitDepth enum for buffer format config
- `src/window.cpp` - colorBitDepthToColorFormat() mapping (line 97-105)
- `apps/simplecube/main.cpp` - Current postProcess implementation example

## Implementation Checklist

- [ ] Create example HDR config file (config/single_bloom.json with bufferBitDepth)
- [ ] Create bloom_shaders.h with all shader source code
- [ ] Implement BloomEffect class (bloom_effect.h/cpp)
- [ ] Add framebuffer creation with RGB16F support
- [ ] Implement bright pass shader
- [ ] Implement blur sampling shader
- [ ] Implement upsample shader with optional tent filter
- [ ] Implement composite shader with ACES tone mapping
- [ ] Integrate into simplecube main.cpp
- [ ] Add keyboard controls (B, ↑, ↓)
- [ ] Test performance at 1080p, 4K
- [ ] Add LDR fallback detection (check input format)
- [ ] Add resize support for window changes
- [ ] Create visual reference screenshots
- [ ] Document bloom parameters in CLAUDE.md
- [ ] Profile with NSight/RenderDoc
- [ ] Test across different GPUs (NVIDIA, AMD, Intel)
- [ ] Test with all projection types (fisheye, cylindrical, etc.)

## Success Criteria

✅ **Performance**: < 2ms at 1080p on GTX 1060 or equivalent
✅ **Visual Quality**: Smooth, natural-looking glow on bright objects
✅ **Integration**: Clean code, no SGCT modifications required (ideal)
✅ **Adjustability**: Real-time parameter changes work smoothly
✅ **Stability**: No crashes, leaks, or visual artifacts
✅ **Documentation**: Code is well-commented and documented in CLAUDE.md

---

**Status**: Design Phase
**Next Step**: Begin implementation of BloomEffect class
**Owner**: TBD
**Target Completion**: TBD

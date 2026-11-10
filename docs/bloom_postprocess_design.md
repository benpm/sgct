# Post-processing and the bloom example

This fork of SGCT adds a post-processing stage to the render pipeline and an example that
uses it for a bloom effect. This document describes both as they are implemented.

## The post-process callback

`Engine::Callbacks::postProcess` has the type `Engine::PostProcessFunction`:

```cpp
void postProcess(const sgct::Window& window, sgct::FrustumMode frustum,
                 unsigned int texture, glm::ivec2 size);
```

`Window::renderViewports` calls it after the scene of all viewports has been rendered
(including the compositing of nonlinear projections such as fisheye or spherical mirror)
and after a multisampled framebuffer has been resolved, but before FXAA and the 2D pass.

- **Frequency.** It is called once per eye texture. Mono windows and side-by-side or
  top-bottom stereo windows produce one call per frame, the latter with
  `FrustumMode::StereoRight` and a texture that contains both eyes. Stereo modes that
  render each eye into its own texture produce one call per eye.
- **Input.** `texture` holds the rendered frame and is `size` pixels large. Its format
  follows the window's `bufferbitdepth` setting, so a window with `"bufferbitdepth": "16f"`
  hands over values above 1.
- **Output.** SGCT binds the framebuffer that the result has to be written into, sets the
  viewport to cover it, and disables blending. The callback usually draws a full-screen
  quad, for example with `Window::renderScreenQuad`. A callback that renders into its own
  framebuffers has to restore the framebuffer binding and the viewport before it returns.
- **Chaining.** SGCT ping-pongs between the window's intermediate texture and the eye
  texture. The callback and FXAA each take one step, and the scene is rendered into the
  texture that makes the chain end in the eye texture, so no extra copy is needed.

## The bloom example

`examples/simplecube` renders a grid of lit cubes and applies `BloomEffect`
(`bloom_effect.h/.cpp`) in its post-process callback. The shaders are embedded in
`bloom_effect.cpp`. The passes are:

1. **Bright pass** at half resolution into an `RGB16F` texture with a mip chain. Pixels
   contribute through a smoothstep around `threshold` with width `softThreshold`, clamped
   to `maxBrightness`.
2. **Mip generation** with `glGenerateMipmap`. Each level is a wider box blur.
3. **Blur** at half resolution, which sums the first `mipLevels` levels with fixed
   Gaussian-like weights. This approximates a very wide blur at a fraction of the cost of
   a separable Gaussian.
4. **Upsample** to full resolution with a 3x3 tent filter.
5. **Composite** of scene and bloom (`bloomStrength`), followed by `exposure`, the ACES
   filmic curve, and gamma encoding into an `RGBA8` texture.

The callback then draws the composite, or one of the intermediate textures selected in
the ImGui panel, into the framebuffer that SGCT has bound. The panel exposes all settings
and four presets. Keys: `H` toggles the panel, `B` toggles bloom, and the arrow keys change
the strength.

## Running it

```sh
cmake --preset linux-ninja-release -DSGCT_BUILD_EXAMPLES=ON -DVCPKG_MANIFEST_FEATURES=examples
cmake --build --preset linux-ninja-release --target simplecube
cd build/linux-ninja-release/bin
./simplecube --config ../../../config/single_bloom.json
```

`config/single_bloom.json` uses a 16-bit float framebuffer so that bright areas keep
values above 1 for the bright pass. With an 8-bit framebuffer the effect still works, but
only pixels close to white bloom. Add `--capture-frame 30 --exit-after-frame 32` to render
a screenshot without user interaction.

# CLAUDE.md

Guidance for Claude Code when working in this repository (the `benpm/sgct` fork of
[SGCT](https://github.com/sgct/sgct)). Read `AGENTS.md` first; it holds the build, test and
style rules and the list of what this fork adds on top of upstream.

## Overview

SGCT (Simple Graphics Cluster Toolkit) is a C++23 library for synchronized OpenGL rendering
across a cluster of machines, used for domes, CAVEs, stereo walls and multi-projector
setups. OpenSpace is its main consumer.

## Build

All third-party dependencies come from vcpkg through `vcpkg.json`; `VCPKG_ROOT` must point
at a vcpkg checkout. The presets in `CMakePresets.json` configure into `build/<preset>/`
and put binaries into `build/<preset>/bin/`.

```sh
cmake --preset linux-ninja-release          # or linux-ninja-debug, windows-msvc, ...
cmake --build --preset linux-ninja-release
build/linux-ninja-release/bin/SGCTTest      # unit tests
python support/check_style_guide.py         # style check
```

The examples need an extra manifest feature and CMake option:

```sh
cmake --preset linux-ninja-release -DSGCT_BUILD_EXAMPLES=ON -DVCPKG_MANIFEST_FEATURES=examples
cmake --build --preset linux-ninja-release --target simplecube
build/linux-ninja-release/bin/simplecube -config config/single.json
```

Every SGCT application takes `-config <file>` to select the cluster configuration. The
example configurations live in `config/`; `config/single_bloom.json` uses a 16-bit float
framebuffer, which the post-processing effects benefit from.

## Architecture in one page

- **Cluster → Node → Window → Viewport.** `config::Cluster` is parsed from JSON
  (`src/config.cpp`, validated against `sgct.schema.json`). One node is the server; the
  others receive the shared state every frame through `Engine::Callbacks::encode/decode`.
- **Engine** (`include/sgct/engine.h`) owns the render loop and the callbacks. The order per
  frame is `preSync → (sync) → postSyncPreDraw → draw (per window, eye, viewport) →
  postProcess → FXAA → draw2D → postDraw → swap`.
- **Window::renderViewports** (`src/window.cpp`) renders all viewports of one eye into the
  eye texture, resolving MSAA if needed, and then runs the post-scene passes. The
  post-process callback and FXAA ping-pong between the `intermediate` and the eye texture;
  the starting target is chosen so the chain ends in the eye texture. Nonlinear projections
  (fisheye, spherical mirror, cylindrical, equirectangular, cubemaps) render their faces
  internally and composite into the viewport before this stage, so post-processing applies
  uniformly to all projection types.
- **Post-process callback.** `Engine::PostProcessFunction` is
  `void(const Window&, FrustumMode, unsigned int texture, glm::ivec2 size)`. SGCT binds the
  destination framebuffer and viewport and disables blending before the call; the callback
  draws a full-screen quad (`Window::renderScreenQuad`) with its result. A callback that
  uses its own framebuffers has to restore `GL_FRAMEBUFFER_BINDING` and the viewport.
  `examples/simplecube/bloom_effect.cpp` is the reference implementation.
- **Math** uses glm through `include/sgct/glm.h`; do not include `glm/glm.hpp` directly.

## Unattended testing

`--capture-frame <n>` saves a screenshot of every window on frame `n` and
`--exit-after-frame <n>` stops the application after `n` frames:

```sh
build/linux-ninja-release/bin/simplecube -config config/single_bloom.json \
    --capture-frame 30 --exit-after-frame 32 --screenshot-path /tmp/shots
```

Inspect the PNGs to verify a rendering change for a given configuration. The visual
regression tests in `visualtests/` are run with `support/runner` (see `AGENTS.md`).

## Conventions

- 90-column lines, 4-space indent, the SGCT copyright box at the top of every file, no
  trailing whitespace. `support/check_style_guide.py` enforces this.
- Match the surrounding code; prefer `std::format` for messages and `Log::Info/Warning/Error`
  for output.
- Config format changes must update `sgct.schema.json`, `config/` and `tests/`.

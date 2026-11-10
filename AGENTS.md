# AGENTS.md
## Build & test
### Windows
- Configure: `cmake --preset windows-msvc`
- Build: `cmake --build --preset windows-msvc-debug`
- Test: `build/windows-msvc/bin/RelWithDebInfo/SGCTTest`
- Style check: `python support/check_style_guide.py`

### Linux
- Configure: `cmake --preset linux-ninja-debug`
- Build: `cmake --build --preset linux-ninja-debug`
- Test: `build/linux-ninja-debug/bin/SGCTTest`
- Image tests: `python support/runner/main.py` (needs the `calibrator` target)
- Style check: `python support/check_style_guide.py`

## Rules
- The image tests are configuration files in `visualtests/<group>/`, which the runner in `support/runner` renders with the calibrator and submits to the regression server (https://regression.openspaceproject.com). The server compares them with the reference images of each hardware, so no reference images are stored in this repository. Changes to the rendering must update or add image tests. Without a `config.json`, the runner stores the images in `support/runner/results`; inspect them for new or changed tests, as their first submission becomes the reference image. The C++ tests in `tests/visual` only contain checks that do not compare with reference images.
- Config file format changes must update `sgct.schema.json`, the example files in `config/`, and the tests in `tests/`.
- Verify every user-facing change (config options, command-line arguments, public API, behavior) against the documentation at https://github.com/sgct/sgct.dev. If the docs are outdated by the change, update that repository as well.

## Fork additions (benpm/sgct)
This fork adds a post-processing stage to the render pipeline and two command-line flags for unattended testing. Keep them working when merging upstream.
- **`Engine::Callbacks::postProcess`** (`Engine::PostProcessFunction`): called once per eye texture after the scene is rendered (once per window for mono and side-by-side/top-bottom stereo, where the texture holds both eyes), before FXAA and the 2D pass, with the window, the frustum mode, the texture holding the frame, and its size. The callback draws the processed image into the framebuffer that is bound when it is called (viewport already set, blending disabled). The passes ping-pong between the intermediate and the eye texture in `Window::renderViewports`; the intermediate texture is created in `Window::createTextures` whenever the callback or FXAA is active.
- **`--capture-frame <n>`** takes a screenshot of all windows on the given (1-based) frame and **`--exit-after-frame <n>`** terminates after that many frames. Together they allow rendering a configuration to a PNG without user interaction.
- **Examples** live in `examples/` and are built with `-DSGCT_BUILD_EXAMPLES=ON -DVCPKG_MANIFEST_FEATURES=examples`. `examples/simplecube` demonstrates the post-process callback with a bloom effect (`docs/bloom_postprocess_design.md`) controlled through an ImGui panel.

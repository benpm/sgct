# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

SGCT (Simple Graphics Cluster Toolkit) is a cross-platform C++ library for developing synchronized OpenGL applications across clusters of image generating computers. It's designed for immersive real-time applications like VR, planetariums, fisheye projections, and stereoscopic displays.

## Build System

SGCT uses CMake (minimum version 3.25).

### Basic Build Commands

The project uses CMake presets (defined in `CMakePresets.json`):

```bash
# Configure (creates build/debug directory)
cmake --preset debug

# Build (builds sgct library and simplecube by default)
cmake --build --preset debug

# Build with examples enabled (first time)
cmake --preset debug -DSGCT_EXAMPLES=ON
cmake --build --preset debug
```

The `debug` preset:
- Configures for Visual Studio 2026 generator on Windows (adapts to native build system on Linux/macOS)
- Enables examples (`SGCT_EXAMPLES=ON`)
- Uses static linking (`BUILD_SHARED_LIBS=OFF`)
- Places executables in `bin/Debug/`

## Architecture

### Core Cluster Concepts

SGCT uses a hierarchical cluster architecture:
- **Cluster** - The entire distributed system
- **Node** - A single computer in the cluster (1+ per cluster)
- **Window** - An OpenGL window (1+ per node)
- **Viewport** - A rendering viewport within a window (1+ per window)
- **Subviewport** - Automatically created for certain projections like fisheye

One node is designated as the **server** (master), others are **clients**. Data flows from server to clients, synchronized automatically by SGCT.

### Main Components

- **Engine** (`include/sgct/engine.h`, `src/engine.cpp`) - Central component managing callbacks, rendering, network, and input. Singleton accessed via `Engine::instance()`.
- **ClusterManager** - Manages nodes and cluster-wide state
- **NetworkManager** - Handles server-client synchronization
- **Window** - Manages OpenGL windows and their viewports
- **Viewport** - Handles rendering into specific viewport regions with projections

### Configuration System

SGCT applications are configured via JSON files (see `config/*.json` for examples):
- Defines cluster topology (nodes, windows, viewports)
- Specifies projection types (planar, fisheye, cylindrical, equirectangular, cubemap, spherical mirror)
- Sets rendering parameters, stereo modes, and user positions
- Allows deployment to different environments without recompilation

Configuration is loaded via `sgct::loadCluster(path)` which returns a `config::Cluster` object.

**Available example configurations** (in `config/`):
- `single.json` - Basic single window, planar projection
- `single_fisheye.json` - 180° fisheye projection (512×512)
- `single_fisheye_fxaa.json` - Fisheye with FXAA anti-aliasing
- `single_cylindrical.json` - Cylindrical projection (panoramic)
- `single_equirectangular.json` - 360° equirectangular (spherical)
- `single_fxaa.json` - Planar with FXAA
- `single_texturemapped.json` - Mesh-based warping/blending
- `single_sbs_stereo.json` - Side-by-side stereoscopic
- `single_two_win.json` / `single_two_win_3D.json` - Multi-window configurations
- `two_nodes.json` - Two-node cluster setup (demonstrates master/client)
- `spherical_mirror.json` / `spherical_mirror_4meshes.json` - Spherical mirror projection
- `spout_output_*.json` - Spout streaming configurations (Windows only)
- `3DTV.json`, `Kinect.json`, `multi_window.json` - Various specialized setups

### Callback-Based Architecture

SGCT applications implement these callbacks registered via `Engine::Callbacks`:

**Essential callbacks:**
- `initOpenGL` - Initialize OpenGL resources (VAOs, VBOs, shaders)
- `draw(RenderData&)` - Render the scene using provided projection matrices
- `preSync` - Called before synchronization (master only should update state here)
- `encode()` - Serialize state to send from server to clients
- `decode(data)` - Deserialize received state on clients

**Optional callbacks:**
- `postSyncPreDraw` - After sync, before draw
- `postDraw` - After draw, before buffer swap
- `postProcess(const Window&, FrustumMode, unsigned int, ivec2)` - Apply post-processing effects to the rendered frame (receives input texture, renders to current framebuffer)
- `draw2D(RenderData&)` - Render 2D overlays/HUDs after post-effects
- `cleanup` - Release OpenGL resources
- `preWindow` - Called before window creation (before OpenGL context exists)
- `keyboard`, `character`, `mouseButton`, `mousePos`, `scroll` - Input handling

The synchronization model: Server runs `preSync` → `encode`, sends data to clients, clients `decode`, then all nodes `draw` in sync.

## Project Structure

```
sgct/
├── include/sgct/          # Public API headers
│   ├── engine.h           # Main Engine class
│   ├── config.h           # Configuration data structures
│   ├── shareddata.h       # Shared data helpers
│   ├── projection/        # Projection types (fisheye, cylindrical, etc.)
│   └── correction/        # Warping/blending mesh loaders
├── src/                   # Implementation files (matches include/)
├── apps/                  # Example applications
│   ├── example1/          # Minimal triangle example (best starting template)
│   ├── simplecube/        # Main testbed - grid of boxes with post-processing
│   ├── network/           # Network/sync example
│   ├── simplenavigation/  # Navigation/interaction example
│   ├── heightmapping/     # Terrain rendering
│   ├── spout*/            # Spout integration examples (Windows)
│   └── ...                # 20+ specialized examples
├── config/                # Example JSON configuration files
├── tests/                 # Test suite (Catch2-based)
├── ext/                   # External dependencies
└── support/cmake/         # CMake modules and utilities
```

## Development Workflow

### Running Examples

After building with `SGCT_EXAMPLES=ON`, executables are in `bin/Debug`:

```bash
# Run with default single-window config
./bin/Debug/simplecube

# Run with specific configuration
./bin/Debug/simplecube --config config/single.json

# Run with fisheye projection
./bin/Debug/simplecube --config config/single_fisheye.json
```

### Understanding apps/simplecube (Main Testbed)

`apps/simplecube` is the primary testbed for SGCT development and testing. It demonstrates most core features and serves as a comprehensive example.

**Structure:**
- `main.cpp` - Application entry point with all callback implementations
- `box.h` / `box.cpp` - Box rendering class (VAO/VBO management)
- `CMakeLists.txt` - Build configuration

**What it demonstrates:**

1. **Multi-object Scene**: Creates an 8×8×8 grid of colored cubes (511 boxes total, center omitted)
   - Each box is 0.8 units with 3.0 unit separation
   - Boxes use per-face colors (red, green, blue, yellow, magenta, cyan)
   - Scene rotates around origin on two axes

2. **Complete Callback Implementation**:
   - `initOpenGL` - Sets up geometry, shaders, and post-process shader
   - `draw` - Renders all boxes with proper MVP transforms
   - `postProcess` - Applies color inversion effect as demonstration
   - `postDraw` - Available for additional rendering (currently empty)
   - `preSync/encode/decode` - Synchronizes animation time across cluster
   - `cleanup` - Properly releases OpenGL resources
   - `keyboard` - ESC key handling

3. **Shader Management**:
   - Main shader ("xform"): Basic vertex transformation + color pass-through
   - Post-process shader ("postprocess"): Full-screen quad with color inversion
   - Both shaders defined inline using raw string literals

4. **Post-Processing Pipeline**:
   - Demonstrates the `postProcess` callback
   - Receives rendered frame as texture
   - Applies full-screen effect (color inversion in this case)
   - Uses `Window::renderScreenQuad()` helper for full-screen rendering

5. **OpenGL Best Practices**:
   - VAO/VBO setup with proper attribute pointers
   - Backface culling enabled
   - Depth testing
   - Resource cleanup in destructors
   - Proper bind/unbind patterns

**Key Code Patterns from simplecube:**

```cpp
// Box creation with position offset
boxes.push_back(std::make_unique<Box>(boxSize, position));

// Per-box transformation in draw loop
for (const auto& box : boxes) {
    glm::mat4 boxTransform = glm::translate(scene, box->position());
    const glm::mat4 mvp =
        glm::make_mat4(data.modelViewProjectionMatrix.values.data()) * boxTransform;
    glUniformMatrix4fv(matrixLoc, 1, GL_FALSE, glm::value_ptr(mvp));
    box->draw();
}

// Post-processing setup
void postProcess(const Window& window, FrustumMode, unsigned int inputTexture, ivec2) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);

    const ShaderProgram& ppPrg = ShaderManager::instance().shaderProgram("postprocess");
    ppPrg.bind();
    window.renderScreenQuad();
    ppPrg.unbind();
}
```

**Testing Different Projections with simplecube:**

The grid of boxes provides excellent visual feedback for testing projections. When testing bloom, use an HDR-enabled configuration.

```bash
# Standard planar projection with bloom (requires config/single_bloom.json with HDR)
./bin/Debug/simplecube --config config/single_bloom.json

# Standard configs (no HDR by default)
./bin/Debug/simplecube --config config/single.json
./bin/Debug/simplecube --config config/single_fisheye.json
./bin/Debug/simplecube --config config/single_cylindrical.json
```

**Why simplecube is the main testbed:**
- Complex enough to test performance and rendering pipeline
- Simple enough to understand and modify quickly
- Demonstrates both basic and advanced features
- Visual feedback makes issues immediately apparent
- Tests multiple viewports/projections effectively due to 3D grid structure
- Good for testing post-processing effects (bloom, FXAA, etc.)

**Testing Different Projections with simplecube:**

The grid of boxes provides excellent visual feedback for testing projections. When testing bloom, use an HDR-enabled configuration.

```bash
# Standard planar projection with bloom (requires config/single_bloom.json with HDR)
./bin/Debug/simplecube --config config/single_bloom.json

# Standard configs (no HDR by default)
./bin/Debug/simplecube --config config/single.json
./bin/Debug/simplecube --config config/single_fisheye.json
./bin/Debug/simplecube --config config/single_cylindrical.json
```

### Creating a New Application

1. Include `<sgct/sgct.h>` and `<sgct/opengl.h>`
2. Implement required callbacks: `initOpenGL`, `draw`, `preSync`, `encode`, `decode`
3. In `main()`:
   - Parse arguments: `Configuration config = parseArguments(arg)`
   - Load cluster: `config::Cluster cluster = loadCluster(config.configFilename)`
   - Create Engine::Callbacks and register functions
   - Create engine: `Engine::create(cluster, callbacks, config)`
   - Run: `Engine::instance().exec()`
   - Cleanup: `Engine::destroy()`

See `apps/example1/main.cpp` for the minimal template.

**Command-Line Arguments:**

SGCT provides `parseArguments()` (from `<sgct/commandline.h>`, included via `<sgct/sgct.h>`) which handles:
- `--config <path>` - Specify configuration file
- `--local <id>` - Which node to run (for multi-node configs)
- `--firm-sync` - Enable stricter synchronization
- Various debug and logging options

The returned `Configuration` struct contains the parsed settings passed to `Engine::create()`.

### State Synchronization

**Critical rule:** Only the server (master) should modify shared state.

```cpp
void preSync() {
    if (Engine::instance().isMaster()) {
        // Update shared state here (time, positions, etc.)
        currentTime = time();
    }
}

std::vector<std::byte> encode() {
    std::vector<std::byte> data;
    serializeObject(data, currentTime);
    // Add more variables as needed
    return data;
}

void decode(const std::vector<std::byte>& data) {
    unsigned int pos = 0;
    deserializeObject(data, pos, currentTime);
    // Decode in same order as encode
}
```

### Rendering with Projections

The `draw` callback receives `RenderData` with pre-computed matrices and viewport info:

```cpp
void draw(const RenderData& data) {
    // data.modelMatrix - Model matrix
    // data.viewMatrix - View matrix
    // data.projectionMatrix - Projection matrix
    // data.modelViewProjectionMatrix - Combined MVP matrix (cached)
    // data.window - Reference to current Window
    // data.viewport - Reference to current BaseViewport
    // data.frustumMode - Mono, StereoLeftEye, or StereoRightEye
    // data.bufferSize - Framebuffer size (ivec2)

    glm::mat4 mvp = glm::make_mat4(data.modelViewProjectionMatrix.values.data()) * sceneTransform;
    // Use MVP to render scene
}
```

SGCT handles all viewport/projection setup - just render using the provided matrices. For stereo rendering, `draw` is called twice per viewport (once per eye) with different `frustumMode` values.

## Coding Conventions

- C++20 standard (tests use C++20, but codebase uses C++23 features where available)
- Use postfix increment operators (i++) not prefix (++i) - this is a project convention
- Headers use include guards: `#ifndef __SGCT__CLASSNAME__H__`
- Public API uses `SGCT_EXPORT` macro for proper DLL export on Windows
- Precompiled headers in `src/CMakeLists.txt` include common dependencies

## Dependencies

SGCT includes and manages these external libraries (in `ext/`):
- GLFW - Windowing and input
- GLAD - OpenGL loading
- GLM - Math library
- libpng/zlib - Image loading
- TinyXML2 - Legacy XML config support (deprecated, use JSON)
- nlohmann/json - JSON parsing with schema validation
- Catch2 - Testing framework
- Optional: FreeType, OpenVR, VRPN, Spout, Tracy

Dependencies are handled via CMake `find_package()` with `SGCT_DEP_INCLUDE_*` options controlling which are built internally.

Tests are organized by feature: `test_config_load_*.cpp` files test different aspects of JSON config loading.

## Common Patterns

### Shader Management

```cpp
ShaderManager::instance().addShaderProgram("name", vertexSource, fragmentSource);
const ShaderProgram& prg = ShaderManager::instance().shaderProgram("name");
prg.bind();
// Use shader
prg.unbind();
```

### Texture Management

```cpp
TextureManager::instance().loadTexture("name", "path/to/texture.png");
GLuint texId = TextureManager::instance().texture("name");
```

### Logging

```cpp
Log::Info("Message");
Log::Warning("Warning");
Log::Error("Error");
```

### Window Helpers

```cpp
// Access current window and its properties
const Window& win = data.window;
ivec2 size = win.framebufferSize();

// Render full-screen quad (useful for post-processing)
window.renderScreenQuad();

// Check if running as master/client
if (Engine::instance().isMaster()) {
    // Master-only code
}
```

### Engine Access

```cpp
// Get the singleton Engine instance
Engine& engine = Engine::instance();

// Control execution
engine.terminate(); // Exit the main loop

// Get timing information
double t = time(); // Seconds since program start
const Engine::Statistics& stats = engine.statistics();
double fps = 1.0 / stats.dt();
```

## HDR Rendering and Post-Processing

SGCT supports HDR (High Dynamic Range) rendering through the `bufferBitDepth` configuration option in window configs.

### Enabling HDR

Add to your config JSON:
```json
{
  "windows": [{
    "bufferbitdepth": "16f",  // Options: "8", "16", "16f", "32f", "16i", "32i", "16ui", "32ui"
    "viewports": [...]
  }]
}
```

**Buffer Format Options**:
- `"8"` - GL_RGBA8 (default, LDR, 8-bit per channel)
- `"16"` - GL_RGBA16 (high precision LDR, 16-bit integer per channel)
- `"16f"` - GL_RGBA16F (HDR, half-float, recommended for bloom and HDR effects)
- `"32f"` - GL_RGBA32F (full precision HDR, higher memory/bandwidth cost)
- `"16i"`, `"32i"`, `"16ui"`, `"32ui"` - Integer formats for special use cases

### Post-Processing Pipeline

The `postProcess` callback receives the rendered frame texture:
```cpp
void postProcess(const Window& window, FrustumMode, unsigned int inputTexture, ivec2 size)
```

**Key Points**:
- `inputTexture` is in the format specified by `bufferbitdepth`
- Called after main rendering, before buffer swap
- Should render result to currently bound framebuffer (usually back to screen)
- Use `window.renderScreenQuad()` to draw full-screen effects
- Can chain multiple effects by using intermediate framebuffers

**Example** (see `apps/simplecube/main.cpp`):
```cpp
void postProcess(const Window& window, FrustumMode, unsigned int inputTexture, ivec2) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);

    const ShaderProgram& ppPrg = ShaderManager::instance().shaderProgram("postprocess");
    ppPrg.bind();
    window.renderScreenQuad();  // Renders full-screen quad
    ppPrg.unbind();
}
```

### Bloom Effect Implementation

**simplecube** now includes a full HDR bloom implementation using mip-map Gaussian approximation. See `docs/bloom_postprocess_design.md` for complete technical details.

**Files**:
- `apps/simplecube/bloom_effect.h/cpp` - Bloom effect implementation
- `apps/simplecube/shaders/bloom_*.frag` - Bloom shader stages
- `config/single_bloom.json` - Example HDR config with bloom

**Usage**:
```bash
# Run with bloom enabled (default)
./bin/Debug/simplecube --config config/single_bloom.json

# Keyboard controls:
# H - Toggle ImGui control panel
# B - Toggle bloom on/off
# ↑/↓ - Adjust bloom strength (0.0-1.0)
# T/G - Adjust brightness threshold
```

**ImGui Control Panel**:
The simplecube example includes an ImGui-based control panel for real-time bloom adjustments:
- **Enable Bloom** checkbox - Toggle bloom on/off
- **View Mode** dropdown - Select visualization mode:
  - Composited (Normal) - Final output with bloom
  - Bloom Only - Just the bloom contribution
  - Bright Pass - Threshold extraction result
  - Blur Pass - Mip-sampled blur result
  - Scene Only - Original scene without bloom
- **Threshold Settings** - Adjust luminance threshold and softness
- **Bloom Intensity** - Control strength and max brightness clamping
- **Quality Settings** - Adjust mip levels and tent filter
- **Presets** - Quick preset buttons (Subtle, Normal, Strong, Dreamy)

**Bloom Parameters** (adjustable in `BloomEffect::Settings`):
- `threshold` - Luminance threshold for bloom (default: 1.0)
- `softThreshold` - Smoothness of threshold (default: 0.5)
- `bloomStrength` - Bloom intensity (default: 0.04)
- `maxBrightness` - Clamp to prevent fireflies (default: 10.0)
- `mipLevels` - Number of mip levels to sample (default: 6)
- `useTentFilter` - Enable tent filter on upsample (default: true)

**Performance**: ~1ms at 1080p on modern GPUs (see design doc for details)

## Additional Notes

### Multi-Node Testing

To test cluster synchronization locally with `two_nodes.json`:
1. Run first instance: `./bin/Debug/simplecube --config config/two_nodes.json --local 0`
2. Run second instance: `./bin/Debug/simplecube --config config/two_nodes.json --local 1`
3. Both windows should show synchronized animation

### Viewport and Projection Types

Each viewport in a config file specifies a projection type:
- `PlanarProjection` - Standard perspective projection
- `FisheyeProjection` - Fisheye/dome projection (FOV up to 360°)
- `CylindricalProjection` - Panoramic projection
- `EquirectangularProjection` - Spherical 360° projection
- `SphericalMirrorProjection` - For spherical mirror displays
- Plus texture-mapped/mesh-based warping for arbitrary display surfaces

### Key Files Reference

- `include/sgct/sgct.h` - Main include (brings in most commonly needed headers)
- `include/sgct/engine.h` - Engine class and main API
- `include/sgct/callbackdata.h` - RenderData structure definition
- `include/sgct/window.h` - Window management
- `include/sgct/shadermanager.h` - Shader compilation and management
- `include/sgct/shareddata.h` - Helper macros for network synchronization
- `include/sgct/commandline.h` - Command-line argument parsing
- `src/engine.cpp` - Engine implementation (main rendering loop)

### Testing Changes

When modifying SGCT core:
1. Build with `cmake --build --preset debug`
2. Run `simplecube` with various configs to test rendering pipeline
3. Run tests with `ctest` from the build directory
4. Test different projection types to ensure changes work universally

### Performance Considerations

- Synchronization happens every frame - minimize data in encode/decode
- Draw callback may be called multiple times per frame (stereo, multiple viewports)
- Post-processing adds overhead - texture copies can be expensive
- Fisheye/cylindrical projections render to cubemap internally (6 rendering passes)

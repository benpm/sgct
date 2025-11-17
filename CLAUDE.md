# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

SGCT (Simple Graphics Cluster Toolkit) is a cross-platform C++ library for developing synchronized OpenGL applications across clusters of image generating computers. It's designed for immersive real-time applications like VR, planetariums, fisheye projections, and stereoscopic displays.

## Build System

SGCT uses CMake (minimum version 3.25).

### Basic Build Commands

```bash
# Configure (creates build/Debug directory)
cmake --preset debug

# Build
cmake --build --preset debug
```

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
- `cleanup` - Release OpenGL resources
- `keyboard`, `mouseButton`, `mousePos`, `scroll` - Input handling

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
│   ├── example1/          # Basic triangle example
│   ├── network/           # Network/sync example
│   └── ...                # Various specialized examples
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

### Creating a New Application

1. Include `<sgct/sgct.h>` and `<sgct/opengl.h>`
2. Implement required callbacks: `initOpenGL`, `draw`, `preSync`, `encode`, `decode`
3. In `main()`:
   - Parse arguments: `parseArguments()`
   - Load cluster: `loadCluster(configPath)`
   - Create Engine::Callbacks and register functions
   - Create engine: `Engine::create(cluster, callbacks, config)`
   - Run: `Engine::instance().exec()`
   - Cleanup: `Engine::destroy()`

See `apps/example1/main.cpp` for the minimal template.

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

The `draw` callback receives `RenderData` with pre-computed projection matrices:

```cpp
void draw(const RenderData& data) {
    // data.modelViewProjectionMatrix - Combined MVP matrix
    // data.viewMatrix - View matrix
    // data.projectionMatrix - Projection matrix

    glm::mat4 mvp = glm::make_mat4(data.modelViewProjectionMatrix.values.data()) * sceneTransform;
    // Use MVP to render scene
}
```

SGCT handles all viewport/projection setup - just render using the provided matrices.

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

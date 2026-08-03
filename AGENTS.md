# Building and Running the "simple_cube" Example

To build and run the "simple_cube" example with the SGCT library, follow these instructions.

## Prerequisites

Ensure you have the following installed (preferably via `winget`):
*   CMake (3.25 or higher): `winget install Kitware.CMake`
*   Ninja Build System: `winget install Ninja-build.Ninja`
*   A C++ Compiler (Visual Studio 2022 recommended on Windows)

## Build Instructions (Using Presets)

This project uses `CMakePresets.json` to simplify configuration. The included `debug` preset configures the project with Ninja and enables examples.

Run the following commands from the root of the repository (`c:\Users\ben\projects\sgct`):

```powershell
# 1. Configure the project using the 'debug' preset
# This uses Ninja, enables SGCT_EXAMPLES, and outputs to 'build/debug'
cmake --preset debug

# 2. Build the 'simplecube' target using the 'debug' preset
cmake --build --preset debug
```

## Run Instructions

After a successful build, the executable is placed in the `bin` directory (defined by `CMAKE_RUNTIME_OUTPUT_DIRECTORY`).

```powershell
# Run simplecube with a single window configuration
.\bin\simplecube.exe -config "config/single.json"
```

### Notes
*   If this is the first time building, CMake will likely download necessary dependencies via submodules or `FetchContent`.
*   The `debug` preset disables `BUILD_SHARED_LIBS` and Freetype support by default (as per `CMakePresets.json`).
*   The `-config` argument is essential for SGCT applications to define the window/cluster configuration.

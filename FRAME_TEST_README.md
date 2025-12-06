# Frame Rendering Test

## Overview

A simple validation test added to `apps/simplecube` that verifies the first frame renders correctly by checking that the framebuffer contains varying colors (not just a solid color).

**The test only runs when `--testing` flag is passed on the command line.**

## Implementation

### Location
`apps/simplecube/main.cpp`

### How It Works

1. **Command-Line Flag**: Check for `--testing` argument in main()
2. **First Frame Only**: Test runs once on the first `postDraw()` callback (if `--testing` enabled)
3. **Read Framebuffer**: Uses `glReadPixels()` to read RGB pixel data
4. **Color Variance Check**: Compares all pixels to the first pixel
   - If all pixels match → **FAIL** (solid color = bad render)
   - If pixels vary → **PASS** (scene rendered correctly)
5. **Write Result**: Saves result to `frame_test_result.txt`
6. **Auto-Exit**: Terminates engine after first frame test completes (only in testing mode)

### Code Changes

Added to namespace:
```cpp
bool firstFrameValidated = false;
bool testingMode = false;
```

Modified `main()`:
```cpp
int main(int argc, char** argv) {
    std::vector<std::string> arg(argv + 1, argv + argc);
    
    // Check for --testing flag
    auto it = std::find(arg.begin(), arg.end(), "--testing");
    if (it != arg.end()) {
        testingMode = true;
        arg.erase(it);  // Remove so parseArguments doesn't choke
        Log::Info("Running in testing mode - will exit after first frame");
    }
    
    Configuration config = parseArguments(arg);
    // ...
}
```

Modified `postDraw()`:
```cpp
void postDraw() {
    // Validate first frame rendering (only in testing mode)
    if (testingMode && !firstFrameValidated) {
        // Read framebuffer
        const Window& window = *Engine::instance().thisNode().windows()[0];
        const ivec2 size = window.framebufferResolution();
        std::vector<unsigned char> pixels(size.x * size.y * 3);
        glReadPixels(0, 0, size.x, size.y, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        
        // Check for color variance
        // ... comparison logic ...
        
        // Write result and exit
        std::ofstream resultFile("frame_test_result.txt");
        // ... write PASS or FAIL ...
        Engine::instance().terminate();
    }
}
```

## Usage

### Normal Mode (Interactive)

Run simplecube normally - it will run continuously until ESC is pressed:

```bash
./bin/simplecube
# or with config
./bin/simplecube --config config/single.json
```

### Testing Mode (Auto-Exit After First Frame)

Add the `--testing` flag to enable frame validation:

```bash
cd /home/ben/projects/sgct
rm -f frame_test_result.txt
./bin/simplecube --testing
cat frame_test_result.txt
```

### Testing Mode with Config

```bash
./bin/simplecube --config config/single.json --testing
cat frame_test_result.txt
```

### Expected Output

**Success:**
```
PASS: First frame rendered with varying colors
```

**Failure:**
```
FAIL: First frame is solid color RGB(255, 255, 255)
```

## Test Results

### ✅ Test 1: With --testing flag
- Renders first frame
- Validates colors
- Writes result file
- Auto-exits
- Test: **PASS**

### ✅ Test 2: Without --testing flag (normal mode)
- Renders continuously
- No validation performed
- No result file created
- Runs until user exits (ESC key)
- Test: **PASS** (normal operation)

### ✅ Test 3: --testing with --config
- Works correctly with other arguments
- Config file loaded properly
- Test: **PASS**

## Integration with Build System

The test is integrated into the existing simplecube build:
- No separate test target needed
- `--testing` flag enables test mode
- Auto-exits after first frame in test mode
- Results written to file for automation

## Automated Testing

Can be used in CI/CD:

```bash
#!/bin/bash
# Run test
./bin/simplecube --testing

# Check result
if grep -q "PASS" frame_test_result.txt; then
    echo "Rendering test passed"
    exit 0
else
    echo "Rendering test failed"
    cat frame_test_result.txt
    exit 1
fi
```

Or with timeout for safety:

```bash
#!/bin/bash
timeout 10 ./bin/simplecube --config config/single.json --testing

if [ $? -eq 0 ] && grep -q "PASS" frame_test_result.txt; then
    echo "✓ Frame rendering test passed"
    exit 0
else
    echo "✗ Frame rendering test failed"
    exit 1
fi
```

## Limitations

- Only tests first frame (no animation validation)
- Simple color variance check (doesn't verify specific scene)
- Requires windowing system (can't run headless without virtual display)
- Tests one window only (multi-window setups not validated)

## Future Improvements

1. **Image Comparison**: Compare against reference image
2. **Multi-Frame**: Validate several frames of animation
3. **Checksum**: Compute pixel checksum for exact comparison
4. **Headless Mode**: Add EGL/osmesa support for CI environments
5. **Screenshot**: Save actual rendered frame for manual inspection
6. **Multiple Configs**: Test multiple projection types automatically

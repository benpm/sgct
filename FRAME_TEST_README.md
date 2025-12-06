# Frame Rendering Test

## Overview

A simple validation test added to `apps/simplecube` that verifies the first frame renders correctly by checking that the framebuffer contains varying colors (not just a solid color).

## Implementation

### Location
`apps/simplecube/main.cpp`

### How It Works

1. **First Frame Only**: Test runs once on the first `postDraw()` callback
2. **Read Framebuffer**: Uses `glReadPixels()` to read RGB pixel data
3. **Color Variance Check**: Compares all pixels to the first pixel
   - If all pixels match → **FAIL** (solid color = bad render)
   - If pixels vary → **PASS** (scene rendered correctly)
4. **Write Result**: Saves result to `frame_test_result.txt`
5. **Auto-Exit**: Terminates engine after first frame test completes

### Code Changes

Added to namespace:
```cpp
bool firstFrameValidated = false;
```

Modified `postDraw()`:
```cpp
void postDraw() {
    if (!firstFrameValidated) {
        // Read framebuffer
        const Window& window = *Engine::instance().thisNode().windows()[0];
        const ivec2 size = window.framebufferResolution();
        std::vector<unsigned char> pixels(size.x * size.y * 3);
        glReadPixels(0, 0, size.x, size.y, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        
        // Check for color variance
        bool allSameColor = true;
        // ... comparison logic ...
        
        // Write result and exit
        std::ofstream resultFile("frame_test_result.txt");
        // ... write PASS or FAIL ...
        Engine::instance().terminate();
    }
}
```

## Usage

### Running the Test

```bash
cd /home/ben/projects/sgct
rm -f frame_test_result.txt
timeout 10 ./bin/simplecube
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

### ✅ Working Render Test
- Boxes render correctly
- Multiple colors detected
- Test: **PASS**

### ❌ Broken Render Test
- Drawing code disabled with `if (false)`
- Only white pixels (clear color)
- Test: **FAIL** - RGB(255, 255, 255)

## Integration with Build System

The test is integrated into the existing simplecube build:
- No separate test target needed
- Runs as part of normal simplecube execution
- Auto-exits after first frame
- Results written to file for automation

## Automated Testing

Can be used in CI/CD:

```bash
#!/bin/bash
./bin/simplecube &
sleep 2
if grep -q "PASS" frame_test_result.txt; then
    echo "Rendering test passed"
    exit 0
else
    echo "Rendering test failed"
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

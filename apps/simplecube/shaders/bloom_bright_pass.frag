#version 330 core

uniform sampler2D inputTex;
uniform float threshold;
uniform float softThreshold;
uniform float maxBrightness;

in vec2 uv;
out vec4 fragColor;

void main() {
    vec3 color = texture(inputTex, uv).rgb;
    
    // HDR luminance extraction
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    
    // Soft threshold
    float contribution = smoothstep(threshold - softThreshold, threshold + softThreshold, luminance);
    
    // Extract bright color (preserve HDR values)
    vec3 bright = color * contribution;
    
    // Clamp maximum brightness to prevent fireflies
    bright = min(bright, vec3(maxBrightness));
    
    fragColor = vec4(bright, 1.0);
}

#version 330 core

uniform sampler2D brightTex;

in vec2 uv;
out vec4 fragColor;

void main() {
    // Sample 6 mip levels with Gaussian-like weights
    vec3 bloom = vec3(0.0);
    float totalWeight = 0.0;
    
    // Weights approximate Gaussian distribution
    float weights[6] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216, 0.0027027);
    
    for (int i = 0; i < 6; i++) {
        vec3 sample = textureLod(brightTex, uv, float(i)).rgb;
        bloom += sample * weights[i];
        totalWeight += weights[i];
    }
    
    bloom /= totalWeight;
    
    fragColor = vec4(bloom, 1.0);
}

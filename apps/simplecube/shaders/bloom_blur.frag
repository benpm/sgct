#version 330 core

uniform sampler2D brightTex;
uniform int mipCount;

in vec2 uv;
out vec4 fragColor;

void main() {
    // Sample up to 10 mip levels with Gaussian-like weights
    vec3 bloom = vec3(0.0);
    float totalWeight = 0.0;
    
    // Weights approximate Gaussian distribution (extended)
    float weights[10] = float[](
        0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216, 
        0.0027027, 0.001, 0.0005, 0.0002, 0.0001
    );
    
    // Clamp mipCount to array size
    int iterations = min(mipCount, 10);

    for (int i = 0; i < iterations; i++) {
        vec3 sample = textureLod(brightTex, uv, float(i)).rgb;
        bloom += sample * weights[i];
        totalWeight += weights[i];
    }
    
    bloom /= totalWeight;
    
    fragColor = vec4(bloom, 1.0);
}

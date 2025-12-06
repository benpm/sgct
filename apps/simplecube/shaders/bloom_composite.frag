#version 330 core

uniform sampler2D sceneTex;
uniform sampler2D bloomTex;
uniform float bloomStrength;

in vec2 uv;
out vec4 fragColor;

// ACES Filmic tone mapping
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 scene = texture(sceneTex, uv).rgb;
    vec3 bloom = texture(bloomTex, uv).rgb;
    
    // Combine scene with bloom
    vec3 combined = scene + bloom * bloomStrength;
    
    // Tone mapping
    vec3 tonemapped = ACESFilm(combined);
    
    // Gamma correction
    tonemapped = pow(tonemapped, vec3(1.0 / 2.2));
    
    fragColor = vec4(tonemapped, 1.0);
}

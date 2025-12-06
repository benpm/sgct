#version 330 core

uniform sampler2D bloomTex;

in vec2 uv;
out vec4 fragColor;

void main() {
    // Tent filter for smooth upsampling
    vec2 texelSize = 1.0 / vec2(textureSize(bloomTex, 0));
    
    vec3 tent = vec3(0.0);
    tent += texture(bloomTex, uv + vec2(-1, -1) * texelSize).rgb * 0.0625;
    tent += texture(bloomTex, uv + vec2( 0, -1) * texelSize).rgb * 0.125;
    tent += texture(bloomTex, uv + vec2( 1, -1) * texelSize).rgb * 0.0625;
    tent += texture(bloomTex, uv + vec2(-1,  0) * texelSize).rgb * 0.125;
    tent += texture(bloomTex, uv + vec2( 0,  0) * texelSize).rgb * 0.25;
    tent += texture(bloomTex, uv + vec2( 1,  0) * texelSize).rgb * 0.125;
    tent += texture(bloomTex, uv + vec2(-1,  1) * texelSize).rgb * 0.0625;
    tent += texture(bloomTex, uv + vec2( 0,  1) * texelSize).rgb * 0.125;
    tent += texture(bloomTex, uv + vec2( 1,  1) * texelSize).rgb * 0.0625;
    
    fragColor = vec4(tent, 1.0);
}

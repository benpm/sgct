#version 330 core

uniform sampler2D tex;
in vec2 uv;
out vec4 color;

void main() {
    vec4 originalColor = texture(tex, uv);
    color = vec4(1.0 - originalColor.rgb, 1.0);
}

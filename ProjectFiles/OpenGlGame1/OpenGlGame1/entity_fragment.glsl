#version 330 core
out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D texture0;

void main() {
    vec4 col = texture(texture0, TexCoord);
    FragColor = vec4(1.0,0.0,0.0,1.0) * col;
}
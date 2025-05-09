#version 330 core
out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D texture0;

void main() {
    vec2 flippedTexCoord = vec2(TexCoord.x, 1.0 - TexCoord.y);
    vec4 texColor = texture(texture0, flippedTexCoord);
    FragColor = texColor;
    //FragColor = vec4(TexCoord, 0.0, 1.0); // Visualize the UVs as color

}
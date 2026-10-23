#version 330 core
out vec4 FragColor;

in vec2 UV;
in vec4 Colour;

uniform sampler2D fontTexture;//one channel, 1 where a font pixel is filled

void main()
{
    // Text and solid rectangles share this shader, rectangles sample the all white cell
    FragColor = vec4(Colour.rgb, Colour.a * texture(fontTexture, UV).r);
}

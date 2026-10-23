#version 330 core
layout (location = 0) in vec2 aPos;//pixels
layout (location = 1) in vec2 aUV;
layout (location = 2) in vec4 aColour;

out vec2 UV;
out vec4 Colour;

uniform mat4 projection;

void main()
{
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    UV = aUV;
    Colour = aColour;
}

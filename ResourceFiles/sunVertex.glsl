#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

uniform mat4 view;
uniform mat4 projection;
uniform vec3 sunDirection;
uniform vec3 camPos;

void main()
{
    // Distance doesn't matter, only the size/distance ratio (the angle it covers), depth is forced to the far plane below
    vec3 sunPos = camPos + normalize(sunDirection) * 100.0;

    mat4 billboard = mat4(
        vec4(view[0][0], view[1][0], view[2][0], 0.0), // Right
        vec4(view[0][1], view[1][1], view[2][1], 0.0), // Up
        vec4(view[0][2], view[1][2], view[2][2], 0.0), // Forward
        vec4(sunPos, 1.0)
    );
    vec4 clipPos = projection * view * billboard * vec4(aPos * 10.0, 0.0, 1.0); // Scale sun size
    gl_Position = clipPos.xyww; // z = w so after the divide depth is 1.0, behind everything, like a skybox
    TexCoord = aTexCoord;
}
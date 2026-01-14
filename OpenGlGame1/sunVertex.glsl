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
    vec3 sunPos = camPos + normalize(sunDirection) * 500.0; // Far away in sun direction

    mat4 billboard = mat4(
        vec4(view[0][0], view[1][0], view[2][0], 0.0), // Right
        vec4(view[0][1], view[1][1], view[2][1], 0.0), // Up
        vec4(view[0][2], view[1][2], view[2][2], 0.0), // Forward
        vec4(sunPos, 1.0)
    );
    gl_Position = projection * view * billboard * vec4(aPos * 50.0, 0.0, 1.0); // Scale sun size
    TexCoord = aTexCoord;
}
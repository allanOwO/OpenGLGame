#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 colour;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in vec3 aNormal;

out vec4 objectColour;//colour to go to frag shader, must be same name
out vec2 TexCoord;
out vec3 FragPos;//world space pos
out vec3 Normal;//normal vector

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 cameraPos;
void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    FragPos = vec3(model * vec4(aPos, 1.0)); // World-space position
    Normal = mat3(transpose(inverse(model))) * aNormal;
    objectColour = vec4(colour,1.0);//add the 1.0 for alpha
    TexCoord = aTexCoord;
}
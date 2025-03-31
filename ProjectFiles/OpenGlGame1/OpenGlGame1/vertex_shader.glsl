#version 330 core
layout (location = 0) in ivec3 aPos;
layout (location = 1) in uint aPackedColour;
layout (location = 2) in uvec2 aTexCoord;
layout (location = 3) in int aPackedNormal;

out vec4 objectColour;//colour to go to frag shader, must be same name
out vec2 TexCoord;
out vec3 FragPos;//world space pos
out vec3 Normal;//normal vector
out vec4 FragPosLightSpace;//output the shadwos

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpaceMatrix; 

// Unpack the color from a 32-bit unsigned integer.
vec4 unpackColor(uint packedColour)
{
    // Assuming the format is: lowest 8 bits = r, next = g, next = b, highest 8 bits = a.
    float r = float((packedColour >> 0)  & 0xFFu) / 255.0;
    float g = float((packedColour >> 8)  & 0xFFu) / 255.0;
    float b = float((packedColour >> 16) & 0xFFu) / 255.0;
    float a = float((packedColour >> 24) & 0xFFu) / 255.0;
    return vec4(r, g, b, a);
}

vec3 unpackNormal(int packedNormal)
{
    uint data = uint(packedNormal);

    // Extract 10-bit signed components in the correct order (x, y, z)
    int x = int(data & 0x3FFu);         // Extract bits 0-9 (x)
    int y = int((data >> 10) & 0x3FFu); // Extract bits 10-19 (y)
    int z = int((data >> 20) & 0x3FFu); // Extract bits 20-29 (z)

    // Convert from 10-bit signed values to normalized float (-1 to 1)
    vec3 normal;
    normal.x = (x >= 512) ? float(x - 1024) / 511.0 : float(x) / 511.0;
    normal.y = (y >= 512) ? float(y - 1024) / 511.0 : float(y) / 511.0;
    normal.z = (z >= 512) ? float(z - 1024) / 511.0 : float(z) / 511.0;

    return normalize(normal); // Normalize to ensure unit length
}

void main()
{
    vec3 pos = vec3(aPos);

    gl_Position = projection * view * model * vec4(pos, 1.0);
    FragPos = vec3(model * vec4(pos, 1.0)); // World-space position

    //Unpack and transform the normal.
    vec3 n = unpackNormal(aPackedNormal);
    Normal = mat3(transpose(inverse(model))) * n;


    // Unpack color.
    objectColour = unpackColor(aPackedColour);

    //Unpack texture coordinates from the 16-bit range [0, 65535] to [0, 1].
    TexCoord = vec2(aTexCoord)/ 65535.0;
    FragPosLightSpace = lightSpaceMatrix * vec4(FragPos,1.0);
}
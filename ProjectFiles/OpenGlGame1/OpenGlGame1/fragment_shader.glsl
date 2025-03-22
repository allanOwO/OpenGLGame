#version 330 core 
out vec4 FragColor;

in vec4 objectColour;
in vec2 TexCoord;

uniform vec4 lightColour;
uniform sampler2D ourTexture;

void main()
{
    float ambientStrength = 0.1;
    vec3 ambient = ambientStrength * lightColour.rgb;
    vec3 baseColor = objectColour.rgb * texture(ourTexture, TexCoord).rgb;
    vec3 result = baseColor * ambient;
    FragColor = vec4(result, 1.0);
}
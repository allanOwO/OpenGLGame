#version 330 core 
out vec4 FragColor;

in vec4 objectColour;
in vec2 TexCoord;
in vec3 FragPos;
in vec3 Normal;

uniform vec4 lightColour;
uniform vec3 lightPos;// Light position in world space
uniform vec3 sunDirection;
uniform sampler2D ourTexture;

void main()
{
    //ambient
    float ambientStrength = 0.1;
    vec3 ambient = ambientStrength * lightColour.rgb;

    //diffuse
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(-sunDirection);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColour.rgb;

    // Combine lighting with object color and texture
    vec3 result = (ambient + diffuse) * objectColour.rgb * texture(ourTexture, TexCoord).rgb;

    FragColor = vec4(result, 1.0);
}
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
    float ambientStrength = 0.3;
    vec3 ambient = ambientStrength * lightColour.rgb;

    //diffuse
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(-sunDirection);
    float diff = (dot(norm, lightDir) + 1.0) * 0.5; // Range [0, 1] instead of sharp cutoff
    vec3 diffuse = diff * lightColour.rgb;

    // Combine lighting with object color and texture
    vec3 result = (ambient + diffuse) * objectColour.rgb * texture(ourTexture, TexCoord).rgb;

    FragColor = vec4(result, 1.0);
}
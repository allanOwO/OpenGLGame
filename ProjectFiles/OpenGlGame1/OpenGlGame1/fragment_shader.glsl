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
uniform vec3 cameraPos;

vec3 sunsetColour();

void main()
{
    //ambient
    float ambientStrength = 0.3;
    vec3 ambient = ambientStrength * lightColour.rgb;

    vec3 sunColour = sunsetColour();

    //directional
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(-sunDirection);
    float diff = max(dot(norm, lightDir),0.0f);
    vec3 diffuse = diff * sunColour;

    // Specular lighting
    float shininess = 32.0f; // Higher values give sharper highlights
    float specularStrength = 0.75f;
    vec3 viewDir = normalize(cameraPos - FragPos); // Camera position should be passed to the shader
    vec3 reflectDir = reflect(-lightDir, norm); // Reflection of light direction
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
    vec3 specular = specularStrength * spec * sunColour; 

    // Combine lighting with object color and texture
    vec3 result = (ambient + diffuse + specular) * objectColour.rgb * texture(ourTexture, TexCoord).rgb;

    FragColor = vec4(result, objectColour.a * texture(ourTexture, TexCoord).a);
}

vec3 sunsetColour(){

    float sunAngle = dot(sunDirection, vec3(0.0f,1.0f,0.0f));

    //cosine wave between white and orange sunlight
    float transitionFactor = (sunAngle +1.0f) *0.5f;

    // Blend between white and orange based on the transition factor
    vec3 sunColour = mix(vec3(1.0f, 1.0f, 1.0f), vec3(1.0f, 0.5f, 0.0f), transitionFactor);

    return sunColour;
}
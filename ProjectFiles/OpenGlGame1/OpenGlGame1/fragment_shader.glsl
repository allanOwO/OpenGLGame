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
    vec3 sunColour = sunsetColour();
    vec3 diffuse = vec3(0,0,0);
    vec3 specular = vec3(0,0,0);

    //ambient
    float ambientStrength = 0.3;
    vec3 ambient = ambientStrength * sunColour;

    //directional
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(-sunDirection);
    
    if(lightDir.y > 0.0f){
        
        float diff = max(dot(norm, lightDir),0.0f);
        diffuse = diff * sunColour;

        // Specular lighting
        float shininess = 32.0f; // Higher values give sharper highlights
        float specularStrength = 0.75f;
        vec3 viewDir = normalize(cameraPos - FragPos); // Camera position should be passed to the shader
        vec3 reflectDir = reflect(-lightDir, norm); // Reflection of light direction
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
        specular = specularStrength * spec * sunColour; 
    }

    

    // Combine lighting with object color and texture
    vec3 result = (ambient + diffuse + specular) * objectColour.rgb * texture(ourTexture, TexCoord).rgb;

    FragColor = vec4(result, objectColour.a * texture(ourTexture, TexCoord).a);
}

vec3 sunsetColour(){

    float sunAngle = dot(sunDirection, vec3(0.0f,1.0f,0.0f));

    
    float transitionFactor = (sunAngle +1.0f) *0.5f;

    // Blend between white and orange based on the transition factor
    return mix(vec3(1.0f, 1.0f, 1.0f), vec3(1.0f, 0.5f, 0.0f), transitionFactor);
}
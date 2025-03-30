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

    
    //ambient occlusion, darkens side faces, keeps tops light
    float ao = clamp((Normal.y + 1.0) * 0.5, 0.0, 1.0);  // Range 0..1
    
    float aoFactor = mix(0.5, 1.0, ao);

    //ambient
    float ambientStrength = 0.3;
    vec3 ambient = ambientStrength * sunColour * aoFactor;

    //directional
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(-sunDirection);
    
    //smoothly disables light when crossing horizon
    float lightFactor = smoothstep(0.0,0.1,lightDir.y);
        
        float diff = max(dot(norm, lightDir),0.0f) * lightFactor;
        diffuse = diff * sunColour;

        // Specular lighting
        float shininess = 32.0f; // Higher values give sharper highlights
        float specularStrength = 0.75f;
        vec3 viewDir = normalize(cameraPos - FragPos); // Camera position should be passed to the shader
        vec3 reflectDir = reflect(-lightDir, norm); // Reflection of light direction
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess) * lightFactor;
        specular = specularStrength * spec * sunColour; 
    

    

    // Combine lighting with object color and texture
    vec3 result = (ambient + diffuse + specular) * objectColour.rgb * texture(ourTexture, TexCoord).rgb;

    FragColor = vec4(result, objectColour.a * texture(ourTexture, TexCoord).a);
    //FragColor = texture(ourTexture, TexCoord);//no lighting

}

vec3 sunsetColour(){

    float sunAngle = dot(sunDirection, vec3(0.0f,1.0f,0.0f));

    
    float transitionFactor = (sunAngle +1.0f) *0.5f;

    // Blend between white and orange based on the transition factor
    return mix(vec3(1.0f, 1.0f, 1.0f), vec3(1.0f, 0.5f, 0.0f), transitionFactor);
}
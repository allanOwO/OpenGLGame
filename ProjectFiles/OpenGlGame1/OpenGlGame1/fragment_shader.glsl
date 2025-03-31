#version 330 core 
out vec4 FragColor;

in vec4 objectColour;
in vec2 TexCoord;
in vec3 FragPos;
in vec3 Normal;
in vec4 FragPosLightSpace;

uniform vec4 lightColour;
uniform vec3 lightPos;// Light position in world space
uniform vec3 sunDirection;
uniform sampler2D ourTexture;
uniform sampler2D shadowMap;
uniform vec3 cameraPos;

vec3 sunsetColour();
float ShadowCalc(vec4 FragPosLightSpace);

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
    float lightFactor = smoothstep(-0.1,0.1,lightDir.y);
        
        float diff = max(dot(norm, lightDir),0.0f) * lightFactor;
        //float diff = 1.0f;
        diffuse = diff * sunColour;

        // Specular lighting
        float shininess = 32.0f; // Higher values give sharper highlights
        float specularStrength = 0.75f;
        vec3 viewDir = normalize(cameraPos - FragPos); // Camera position should be passed to the shader
        vec3 reflectDir = reflect(-lightDir, norm); // Reflection of light direction
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess) * lightFactor;
        specular = specularStrength * spec * sunColour; 
    
    // Calculate shadow
    float shadow = ShadowCalc(FragPosLightSpace);
    
    // Combine lighting with shadow
    vec3 lighting = (ambient + (1.0 - shadow) * (diffuse + specular));

    // Combine lighting with object color and texture
    vec3 result = lighting * objectColour.rgb * texture(ourTexture, TexCoord).rgb;

    FragColor = vec4(result, objectColour.a * texture(ourTexture, TexCoord).a);
    //FragColor = texture(ourTexture, TexCoord);//no lighting

}

float ShadowCalc(vec4 fragPosLightSpace)
{
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w; // Perspective divide
    projCoords = projCoords * 0.5 + 0.5; // Transform to [0,1]
    if (projCoords.z > 1.0) return 0.0; // Outside far plane, no shadow

    //smooth shadows
    float shadow = 0.0;
    float bias = 0.005;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += projCoords.z - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0; // Average over 3x3 grid


    return shadow;
} 

vec3 sunsetColour(){

    float sunAngle = dot(sunDirection, vec3(0.0f,1.0f,0.0f));

    
    float transitionFactor = (sunAngle +1.0f) *0.5f;

    // Blend between white and orange based on the transition factor
    return mix(vec3(1.0f, 1.0f, 1.0f), vec3(1.0f, 0.5f, 0.0f), transitionFactor);
}
#version 330 core 
out vec4 FragColour;

in vec2 TexCoord;

uniform vec3 sunColour;

void main()
{
    float dist = length(TexCoord - vec2(0.5));
    float radius = 0.4;
    float softness = 0.15;
    float sunAlpha = smoothstep( radius, radius + softness, dist);
    if (dist > radius + softness) discard;
    FragColour = vec4(sunColour, 1.0 - sunAlpha);
   // FragColour = vec4(1.0, 0.0, 0.0, 1.0); // Solid red for testing
}
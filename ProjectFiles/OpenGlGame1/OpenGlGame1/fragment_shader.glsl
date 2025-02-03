#version 330 core 
out vec4 FragColor;

in vec4 ourColour;
in vec2 TexCoord;

uniform sampler2D ourTexture;

void main()
{
	FragColor = texture(ourTexture,TexCoord) * ourColour;
};
#version 450

layout (binding = 1) uniform sampler2D texSampler;

layout (location = 0) in vec3 inColor;
layout (location = 1) in vec3 inTexCoord;

layout (location = 0) out vec4 outFragColor;

void main()
{
	outFragColor = texture(texSampler, inTexCoord) * vec4(inColor, 1.0);
}
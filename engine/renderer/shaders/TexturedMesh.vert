#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec3 outNormal;

layout(push_constant) uniform DrawPush {
    mat4 modelViewProjection;
    vec4 baseColor;
    vec4 pbrFactors;
    vec4 emissiveColor;
} pushData;

void main()
{
    gl_Position = pushData.modelViewProjection * vec4(inPosition, 1.0);
    outTexCoord = inTexCoord;
    outNormal = inNormal;
}

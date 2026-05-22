#version 450

layout(set = 0, binding = 0) uniform sampler2D baseColorTexture;

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform DrawPush {
    mat4 modelViewProjection;
    vec4 baseColor;
} pushData;

void main()
{
    vec3 lightDirection = normalize(vec3(0.4, 0.8, 0.25));
    float lambert = max(dot(normalize(inNormal), lightDirection), 0.0);
    float lighting = 0.55 + lambert * 0.45;
    outColor = texture(baseColorTexture, inTexCoord) * pushData.baseColor * vec4(vec3(lighting), 1.0);
}

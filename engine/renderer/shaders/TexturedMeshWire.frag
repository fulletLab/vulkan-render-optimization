#version 450

layout(set = 0, binding = 1) uniform sampler2D baseColorTexture;

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColorFactor;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in vec3 inWorldPosition;
layout(location = 5) in vec4 inMaterialFactors;
layout(location = 6) in vec3 inBarycentric;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform DrawPush {
    mat4 modelMatrix;
    vec4 baseColor;
    vec4 pbrFactors;
    vec4 emissiveColor;
    vec4 materialExtras;
} pushData;

float wireCoverage()
{
    vec3 derivative = fwidth(inBarycentric);
    vec3 edge = smoothstep(vec3(0.0), derivative * 1.35, inBarycentric);
    return 1.0 - min(min(edge.x, edge.y), edge.z);
}

void main()
{
    vec4 sampledBase = texture(baseColorTexture, inTexCoord) * pushData.baseColor * inColorFactor;
    int alphaMode = int(pushData.pbrFactors.w + 0.5);
    if (alphaMode == 1 && sampledBase.a < pushData.emissiveColor.a) {
        discard;
    }

    float coverage = wireCoverage();
    if (coverage <= 0.02) {
        discard;
    }
    outColor = vec4(pushData.baseColor.rgb, sampledBase.a * coverage);
}

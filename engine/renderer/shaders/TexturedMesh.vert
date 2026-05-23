#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inColor;
layout(location = 4) in vec4 inTangent;
layout(location = 5) in vec4 inMaterialFactors;
layout(location = 6) in vec4 inModel0;
layout(location = 7) in vec4 inModel1;
layout(location = 8) in vec4 inModel2;
layout(location = 9) in vec4 inModel3;

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec4 outColorFactor;
layout(location = 3) out vec4 outTangent;
layout(location = 4) out vec3 outWorldPosition;
layout(location = 5) out vec4 outMaterialFactors;

struct FrameLight {
    vec4 positionType;
    vec4 directionRange;
    vec4 colorIntensity;
    vec4 spotAngles;
};

layout(set = 0, binding = 0) uniform FrameData {
    mat4 viewProjection;
    mat4 shadowViewProjection;
    vec4 cameraPositionLightCount;
    vec4 ambientSky;
    vec4 ambientGround;
    vec4 shadowSettings;
    FrameLight lights[8];
} frameData;

layout(push_constant) uniform DrawPush {
    mat4 modelMatrix;
    vec4 baseColor;
    vec4 pbrFactors;
    vec4 emissiveColor;
    vec4 materialExtras;
} pushData;

void main()
{
    mat4 instanceModel = mat4(inModel0, inModel1, inModel2, inModel3);
    vec4 worldPosition = instanceModel * vec4(inPosition, 1.0);
    mat3 normalMatrix = transpose(inverse(mat3(instanceModel)));
    gl_Position = frameData.viewProjection * worldPosition;
    outTexCoord = inTexCoord;
    outNormal = normalize(normalMatrix * inNormal);
    outColorFactor = inColor;
    outTangent = vec4(normalize(mat3(instanceModel) * inTangent.xyz), inTangent.w);
    outWorldPosition = worldPosition.xyz;
    outMaterialFactors = inMaterialFactors;
}

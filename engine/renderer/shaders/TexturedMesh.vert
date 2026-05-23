#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inColor;
layout(location = 4) in vec4 inTangent;

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec4 outColorFactor;
layout(location = 3) out vec4 outTangent;
layout(location = 4) out vec3 outWorldPosition;

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
    vec4 worldPosition = pushData.modelMatrix * vec4(inPosition, 1.0);
    mat3 normalMatrix = transpose(inverse(mat3(pushData.modelMatrix)));
    gl_Position = frameData.viewProjection * worldPosition;
    outTexCoord = inTexCoord;
    outNormal = normalize(normalMatrix * inNormal);
    outColorFactor = inColor;
    outTangent = vec4(normalize(mat3(pushData.modelMatrix) * inTangent.xyz), inTangent.w);
    outWorldPosition = worldPosition.xyz;
}

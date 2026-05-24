#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 6) in vec4 inModel0;
layout(location = 7) in vec4 inModel1;
layout(location = 8) in vec4 inModel2;
layout(location = 9) in vec4 inModel3;

layout(location = 0) out vec2 outTexCoord;

struct FrameLight {
    vec4 positionType;
    vec4 directionRange;
    vec4 colorIntensity;
    vec4 spotAngles;
};

layout(set = 0, binding = 0) uniform FrameData {
    mat4 viewProjection;
    mat4 shadowViewProjection;
    mat4 shadowViewProjections[6];
    vec4 cameraPositionLightCount;
    vec4 ambientSky;
    vec4 ambientGround;
    vec4 shadowSettings;
    vec4 shadowCascadeSplits;
    vec4 shadowAtlasSettings;
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
    int viewCount = max(int(frameData.shadowAtlasSettings.x + 0.5), 1);
    int viewIndex = clamp(int(pushData.materialExtras.w + 0.5), 0, viewCount - 1);
    gl_Position = frameData.shadowViewProjections[viewIndex] * instanceModel * vec4(inPosition, 1.0);
    outTexCoord = inTexCoord;
}

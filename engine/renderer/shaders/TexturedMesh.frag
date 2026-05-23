#version 450

layout(set = 0, binding = 1) uniform sampler2D baseColorTexture;
layout(set = 0, binding = 2) uniform sampler2D normalTexture;
layout(set = 0, binding = 3) uniform sampler2D metallicRoughnessTexture;
layout(set = 0, binding = 4) uniform sampler2D occlusionTexture;
layout(set = 0, binding = 5) uniform sampler2D emissiveTexture;
layout(set = 0, binding = 6) uniform sampler2D shadowMap;
layout(set = 0, binding = 7) uniform sampler2D brdfLutTexture;
layout(set = 0, binding = 8) uniform samplerCube irradianceMap;
layout(set = 0, binding = 9) uniform samplerCube prefilteredEnvironmentMap;

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColorFactor;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in vec3 inWorldPosition;
layout(location = 5) in vec4 inMaterialFactors;

layout(location = 0) out vec4 outColor;

const float pi = 3.14159265359;

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

float distributionGGX(vec3 normal, vec3 halfVector, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH = max(dot(normal, halfVector), 0.0);
    float denominator = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(pi * denominator * denominator, 0.0001);
}

float geometrySchlickGGX(float nDotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) * 0.125;
    return nDotV / max(nDotV * (1.0 - k) + k, 0.0001);
}

float geometrySmith(vec3 normal, vec3 viewDirection, vec3 lightDirection, float roughness)
{
    return geometrySchlickGGX(max(dot(normal, viewDirection), 0.0), roughness)
        * geometrySchlickGGX(max(dot(normal, lightDirection), 0.0), roughness);
}

vec3 fresnelSchlick(float cosine, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}

float rangeAttenuation(float distanceToLight, float range)
{
    float inverseSquare = 1.0 / max(distanceToLight * distanceToLight, 0.01);
    if (range <= 0.0) {
        return inverseSquare;
    }
    float normalized = clamp(distanceToLight / range, 0.0, 1.0);
    float smoothRange = clamp(1.0 - normalized * normalized * normalized * normalized, 0.0, 1.0);
    return inverseSquare * smoothRange * smoothRange;
}

float spotAttenuation(FrameLight light, vec3 lightDirection)
{
    float lightForwardDot = dot(normalize(light.directionRange.xyz), -lightDirection);
    float innerCos = cos(light.spotAngles.x);
    float outerCos = cos(light.spotAngles.y);
    return smoothstep(outerCos, max(innerCos, outerCos + 0.0001), lightForwardDot);
}

float shadowVisibility(int lightIndex, vec3 normal, vec3 lightDirection)
{
    if (frameData.shadowSettings.x < 0.5 || lightIndex != int(frameData.shadowSettings.y + 0.5)) {
        return 1.0;
    }
    vec4 shadowClip = frameData.shadowViewProjection * vec4(inWorldPosition, 1.0);
    if (shadowClip.w <= 0.0) {
        return 1.0;
    }
    vec3 shadowCoord = shadowClip.xyz / shadowClip.w;
    shadowCoord.xy = shadowCoord.xy * 0.5 + 0.5;
    if (any(lessThan(shadowCoord, vec3(0.0))) || any(greaterThan(shadowCoord, vec3(1.0)))) {
        return 1.0;
    }
    float bias = max(frameData.shadowSettings.z * (1.0 - dot(normal, lightDirection)), 0.00025);
    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));
    float visibility = 0.0;
    float totalWeight = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            float weight = 1.0 / (1.0 + 0.55 * float(abs(x) + abs(y)));
            float sampledDepth = texture(shadowMap, shadowCoord.xy + vec2(x, y) * texel).r;
            visibility += (shadowCoord.z - bias <= sampledDepth ? 1.0 : 0.0) * weight;
            totalWeight += weight;
        }
    }
    return visibility / max(totalWeight, 0.0001);
}

void main()
{
    vec4 sampledBase = texture(baseColorTexture, inTexCoord) * pushData.baseColor * inColorFactor;
    int alphaMode = int(pushData.pbrFactors.w + 0.5);
    if (alphaMode == 1 && sampledBase.a < pushData.emissiveColor.a) {
        discard;
    }

    vec3 geometricNormal = normalize(inNormal);
    vec3 tangent = normalize(inTangent.xyz - geometricNormal * dot(geometricNormal, inTangent.xyz));
    vec3 bitangent = normalize(cross(geometricNormal, tangent)) * inTangent.w;
    vec3 textureNormal = texture(normalTexture, inTexCoord).xyz * 2.0 - 1.0;
    textureNormal.xy *= pushData.pbrFactors.z * inMaterialFactors.z;
    vec3 normal = normalize(mat3(tangent, bitangent, geometricNormal) * textureNormal);
    vec3 viewDirection = normalize(frameData.cameraPositionLightCount.xyz - inWorldPosition);
    vec4 pbrTexel = texture(metallicRoughnessTexture, inTexCoord);
    float metallic = clamp(pushData.pbrFactors.x * inMaterialFactors.x * pbrTexel.b, 0.0, 1.0);
    float roughness = clamp(pushData.pbrFactors.y * inMaterialFactors.y * pbrTexel.g, 0.045, 1.0);
    float occlusion = mix(
        1.0,
        texture(occlusionTexture, inTexCoord).r,
        clamp(pushData.materialExtras.x * inMaterialFactors.w, 0.0, 1.0));
    vec3 f0 = mix(vec3(0.04), sampledBase.rgb, metallic);
    vec3 directRadiance = vec3(0.0);

    int lightCount = min(int(frameData.cameraPositionLightCount.w + 0.5), 8);
    for (int index = 0; index < lightCount; ++index) {
        FrameLight light = frameData.lights[index];
        float type = light.positionType.w;
        vec3 lightDirection;
        float attenuation = 1.0;
        if (type < 0.5) {
            lightDirection = normalize(-light.directionRange.xyz);
        } else {
            vec3 offset = light.positionType.xyz - inWorldPosition;
            float distanceToLight = length(offset);
            lightDirection = offset / max(distanceToLight, 0.0001);
            attenuation = rangeAttenuation(distanceToLight, light.directionRange.w);
            if (type > 1.5) {
                attenuation *= spotAttenuation(light, lightDirection);
            }
        }

        vec3 halfVector = normalize(viewDirection + lightDirection);
        float nDotL = max(dot(normal, lightDirection), 0.0);
        if (nDotL <= 0.0 || attenuation <= 0.0) {
            continue;
        }
        vec3 fresnel = fresnelSchlick(max(dot(halfVector, viewDirection), 0.0), f0);
        float ndf = distributionGGX(normal, halfVector, roughness);
        float geometry = geometrySmith(normal, viewDirection, lightDirection, roughness);
        vec3 specular = (ndf * geometry * fresnel)
            / max(4.0 * max(dot(normal, viewDirection), 0.0) * nDotL, 0.0001);
        vec3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * sampledBase.rgb / pi;
        vec3 radiance = light.colorIntensity.rgb * light.colorIntensity.w * attenuation;
        float visibility = shadowVisibility(index, normal, lightDirection);
        directRadiance += (diffuse + specular) * radiance * nDotL * visibility;
    }

    float nDotV = max(dot(normal, viewDirection), 0.0);
    vec3 ambientFresnel = fresnelSchlick(nDotV, f0);
    vec3 diffuseIrradiance = texture(irradianceMap, normal).rgb;
    vec3 reflectionDirection = reflect(-viewDirection, normal);
    vec3 specularIrradiance = textureLod(prefilteredEnvironmentMap, reflectionDirection, roughness * 4.0).rgb;
    vec2 envBRDF = texture(brdfLutTexture, vec2(nDotV, roughness)).rg;
    vec3 ambientDiffuse = diffuseIrradiance * sampledBase.rgb * (1.0 - metallic);
    vec3 ambientSpecular = specularIrradiance * clamp(ambientFresnel * envBRDF.x + vec3(envBRDF.y), vec3(0.0), vec3(1.0));
    vec3 emissive = texture(emissiveTexture, inTexCoord).rgb * pushData.emissiveColor.rgb;
    float outputAlpha = alphaMode == 2 ? sampledBase.a : 1.0;
    outColor = vec4((ambientDiffuse + ambientSpecular) * occlusion + directRadiance + emissive, outputAlpha);
}

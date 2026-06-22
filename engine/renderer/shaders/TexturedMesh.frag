#version 450

layout(set = 0, binding = 1) uniform sampler2D baseColorTexture;
layout(set = 0, binding = 2) uniform sampler2D normalTexture;
layout(set = 0, binding = 3) uniform sampler2D metallicRoughnessTexture;
layout(set = 0, binding = 4) uniform sampler2D occlusionTexture;
layout(set = 0, binding = 5) uniform sampler2D emissiveTexture;
layout(set = 0, binding = 6) uniform sampler2DShadow shadowMap;
layout(set = 0, binding = 7) uniform sampler2D brdfLutTexture;
layout(set = 0, binding = 8) uniform samplerCube irradianceMap;
layout(set = 0, binding = 9) uniform samplerCube prefilteredEnvironmentMap;
layout(set = 0, binding = 10) uniform samplerCubeShadow pointShadowMap;

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
    mat4 shadowViewProjections[6];
    vec4 cameraPositionLightCount;
    vec4 ambientSky;
    vec4 ambientGround;
    vec4 shadowSettings;
    vec4 shadowCascadeSplits;
    vec4 shadowAtlasSettings;
    vec4 debugSettings;
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

float rangeAttenuation(float distanceToLight, float range, float linearFactor, float quadraticFactor)
{
    float customDenominator = 1.0 + max(linearFactor, 0.0) * distanceToLight
        + max(quadraticFactor, 0.0001) * distanceToLight * distanceToLight;
    float inverseSquare = 1.0 / max(customDenominator, 0.01);
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

vec3 shadowCoordForView(int viewIndex)
{
    vec4 shadowClip = frameData.shadowViewProjections[viewIndex] * vec4(inWorldPosition, 1.0);
    if (shadowClip.w <= 0.0) {
        return vec3(-1.0);
    }
    vec3 shadowCoord = shadowClip.xyz / shadowClip.w;
    shadowCoord.xy = shadowCoord.xy * 0.5 + 0.5;
    return shadowCoord;
}

bool validShadowCoord(vec3 shadowCoord)
{
    return all(greaterThanEqual(shadowCoord, vec3(0.0))) && all(lessThanEqual(shadowCoord, vec3(1.0)));
}

int selectShadowCascade(out vec3 shadowCoord)
{
    int cascadeCount = clamp(int(frameData.shadowAtlasSettings.y + 0.5), 1, 4);
    if (cascadeCount <= 1) {
        shadowCoord = shadowCoordForView(0);
        return 0;
    }
    for (int index = 0; index < cascadeCount; ++index) {
        vec3 candidate = shadowCoordForView(index);
        if (validShadowCoord(candidate)) {
            shadowCoord = candidate;
            return index;
        }
    }
    shadowCoord = shadowCoordForView(cascadeCount - 1);
    return cascadeCount - 1;
}

float pointShadowVisibility(FrameLight light, vec3 normal, vec3 lightDirection)
{
    float farPlane = frameData.shadowAtlasSettings.w;
    if (farPlane <= 0.0) {
        return 1.0;
    }
    vec3 fromLight = inWorldPosition - light.positionType.xyz;
    float majorDistance = max(max(abs(fromLight.x), abs(fromLight.y)), abs(fromLight.z));
    if (majorDistance <= 0.05 || majorDistance >= farPlane) {
        return 1.0;
    }
    float nearPlane = 0.05;
    float projectedDepth = farPlane / (farPlane - nearPlane)
        - (nearPlane * farPlane) / ((farPlane - nearPlane) * majorDistance);
    float slope = clamp(1.0 - dot(normal, lightDirection), 0.0, 1.0);
    float bias = max(frameData.shadowSettings.z * (1.0 + slope), 0.0006);
    return texture(pointShadowMap, vec4(fromLight, projectedDepth - bias));
}

float shadowVisibility(int lightIndex, FrameLight light, vec3 normal, vec3 lightDirection)
{
    if (frameData.shadowSettings.x < 0.5 || lightIndex != int(frameData.shadowSettings.y + 0.5)) {
        return 1.0;
    }
    if (int(frameData.shadowSettings.w + 0.5) == 3 && light.positionType.w > 0.5 && light.positionType.w < 1.5) {
        return pointShadowVisibility(light, normal, lightDirection);
    }
    vec3 shadowCoord;
    int cascadeIndex = selectShadowCascade(shadowCoord);
    if (!validShadowCoord(shadowCoord)) {
        return 1.0;
    }
    float slope = clamp(1.0 - dot(normal, lightDirection), 0.0, 1.0);
    float bias = max(frameData.shadowSettings.z * (1.0 + slope * 2.0), 0.00035);
    vec2 atlasScale = vec2(frameData.shadowAtlasSettings.z);
    if (atlasScale.x < 0.99) {
        vec2 margin = (1.0 / vec2(textureSize(shadowMap, 0))) / atlasScale * 2.5;
        shadowCoord.xy = clamp(shadowCoord.xy, margin, vec2(1.0) - margin);
        vec2 tile = vec2(float(cascadeIndex % 2), float(cascadeIndex / 2));
        shadowCoord.xy = shadowCoord.xy * atlasScale + tile * atlasScale;
    }
    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));
    float visibility = 0.0;
    float totalWeight = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            float weight = 1.0 / (1.0 + 0.55 * float(abs(x) + abs(y)));
            visibility += texture(shadowMap, vec3(shadowCoord.xy + vec2(x, y) * texel, shadowCoord.z - bias)) * weight;
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

    int debugMode = int(frameData.debugSettings.x + 0.5);
    if (debugMode == 1) {
        // Stable face/surface-normal visualization: +/-X red/pink, +Y green, +Z blue/teal.
        // Diagonal normals naturally blend the three axes without touching source materials.
        vec3 normalColor = vec3(abs(normal.x), max(normal.y, 0.0), max(normal.z, 0.0));
        normalColor = mix(vec3(0.055), normalColor, 0.92);
        outColor = vec4(normalColor, 1.0);
        return;
    }
    if (debugMode == 2) {
        float depth = pow(clamp(gl_FragCoord.z, 0.0, 1.0), 24.0);
        outColor = vec4(vec3(1.0 - depth), 1.0);
        return;
    }
    if (debugMode == 3) {
        float visibility = 1.0;
        int shadowLight = int(frameData.shadowSettings.y + 0.5);
        if (frameData.shadowSettings.x > 0.5 && shadowLight >= 0
            && shadowLight < min(int(frameData.cameraPositionLightCount.w + 0.5), 8)) {
            FrameLight light = frameData.lights[shadowLight];
            vec3 lightDirection;
            if (light.positionType.w < 0.5) {
                lightDirection = normalize(-light.directionRange.xyz);
            } else {
                lightDirection = normalize(light.positionType.xyz - inWorldPosition);
            }
            visibility = shadowVisibility(shadowLight, light, normal, lightDirection);
        }
        outColor = vec4(vec3(visibility), 1.0);
        return;
    }
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
            attenuation = rangeAttenuation(
                distanceToLight,
                light.directionRange.w,
                light.spotAngles.z,
                light.spotAngles.w);
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
        float visibility = shadowVisibility(index, light, normal, lightDirection);
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
    float outputAlpha = sampledBase.a;
    outColor = vec4((ambientDiffuse + ambientSpecular) * occlusion + directRadiance + emissive, outputAlpha);
}

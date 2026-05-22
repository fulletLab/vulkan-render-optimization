#version 450

layout(set = 0, binding = 0) uniform sampler2D baseColorTexture;
layout(set = 0, binding = 1) uniform sampler2D normalTexture;
layout(set = 0, binding = 2) uniform sampler2D metallicRoughnessTexture;
layout(set = 0, binding = 3) uniform sampler2D occlusionTexture;

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColorFactor;
layout(location = 3) in vec4 inTangent;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform DrawPush {
    mat4 modelViewProjection;
    vec4 baseColor;
    vec4 pbrFactors;
    vec4 emissiveColor;
    vec4 materialExtras;
} pushData;

void main()
{
    vec3 lightDirection = normalize(vec3(0.4, 0.8, 0.25));
    vec3 geometricNormal = normalize(inNormal);
    vec3 tangent = normalize(inTangent.xyz - geometricNormal * dot(geometricNormal, inTangent.xyz));
    vec3 bitangent = normalize(cross(geometricNormal, tangent)) * inTangent.w;
    vec3 textureNormal = texture(normalTexture, inTexCoord).xyz * 2.0 - 1.0;
    textureNormal.xy *= pushData.pbrFactors.z;
    vec3 normal = normalize(mat3(tangent, bitangent, geometricNormal) * textureNormal);
    vec4 materialTexel = texture(metallicRoughnessTexture, inTexCoord);
    float metallic = clamp(pushData.pbrFactors.x * materialTexel.b, 0.0, 1.0);
    float roughness = clamp(pushData.pbrFactors.y * materialTexel.g, 0.04, 1.0);
    float lambert = max(dot(normal, lightDirection), 0.0);
    vec4 sampledBase = texture(baseColorTexture, inTexCoord) * pushData.baseColor * inColorFactor;
    int alphaMode = int(pushData.pbrFactors.w + 0.5);
    if (alphaMode == 1 && sampledBase.a < pushData.emissiveColor.a) {
        discard;
    }
    float rawOcclusion = texture(occlusionTexture, inTexCoord).r;
    float occlusion = mix(1.0, rawOcclusion, clamp(pushData.materialExtras.x, 0.0, 1.0));
    vec3 diffuse = sampledBase.rgb * (0.36 * occlusion + lambert * mix(0.64, 0.28, metallic));
    float specularPower = mix(96.0, 12.0, roughness);
    float specularTerm = pow(max(lambert, 0.0), specularPower) * mix(0.08, 0.55, metallic);
    vec3 emissive = pushData.emissiveColor.rgb;
    float outputAlpha = alphaMode == 2 ? sampledBase.a : 1.0;
    outColor = vec4(diffuse + vec3(specularTerm) + emissive, outputAlpha);
}

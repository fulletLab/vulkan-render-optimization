#version 450

layout(set = 0, binding = 0) uniform sampler2D baseColorTexture;

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColorFactor;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform DrawPush {
    mat4 modelViewProjection;
    vec4 baseColor;
    vec4 pbrFactors;
    vec4 emissiveColor;
} pushData;

void main()
{
    vec3 lightDirection = normalize(vec3(0.4, 0.8, 0.25));
    vec3 normal = normalize(inNormal);
    float metallic = clamp(pushData.pbrFactors.x, 0.0, 1.0);
    float roughness = clamp(pushData.pbrFactors.y, 0.04, 1.0);
    float lambert = max(dot(normal, lightDirection), 0.0);
    vec4 sampledBase = texture(baseColorTexture, inTexCoord) * pushData.baseColor * inColorFactor;
    vec3 diffuse = sampledBase.rgb * (0.36 + lambert * mix(0.64, 0.28, metallic));
    float specularPower = mix(96.0, 12.0, roughness);
    float specularTerm = pow(max(lambert, 0.0), specularPower) * mix(0.08, 0.55, metallic);
    vec3 emissive = pushData.emissiveColor.rgb;
    outColor = vec4(diffuse + vec3(specularTerm) + emissive, sampledBase.a);
}

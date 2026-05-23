#version 450

layout(set = 0, binding = 1) uniform sampler2D baseColorTexture;

layout(location = 0) in vec2 inTexCoord;

layout(push_constant) uniform DrawPush {
    mat4 modelMatrix;
    vec4 baseColor;
    vec4 pbrFactors;
    vec4 emissiveColor;
    vec4 materialExtras;
} pushData;

void main()
{
    int alphaMode = int(pushData.pbrFactors.w + 0.5);
    if (alphaMode == 1) {
        float alpha = texture(baseColorTexture, inTexCoord).a * pushData.baseColor.a;
        if (alpha < pushData.emissiveColor.a) {
            discard;
        }
    }
}

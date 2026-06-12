#version 450

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

layout(location = 0) in vec2 inTexCoord[];
layout(location = 1) in vec3 inNormal[];
layout(location = 2) in vec4 inColorFactor[];
layout(location = 3) in vec4 inTangent[];
layout(location = 4) in vec3 inWorldPosition[];
layout(location = 5) in vec4 inMaterialFactors[];

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec4 outColorFactor;
layout(location = 3) out vec4 outTangent;
layout(location = 4) out vec3 outWorldPosition;
layout(location = 5) out vec4 outMaterialFactors;
layout(location = 6) out vec3 outBarycentric;

void emitWireVertex(int index, vec3 barycentric)
{
    gl_Position = gl_in[index].gl_Position;
    outTexCoord = inTexCoord[index];
    outNormal = inNormal[index];
    outColorFactor = inColorFactor[index];
    outTangent = inTangent[index];
    outWorldPosition = inWorldPosition[index];
    outMaterialFactors = inMaterialFactors[index];
    outBarycentric = barycentric;
    EmitVertex();
}

void main()
{
    emitWireVertex(0, vec3(1.0, 0.0, 0.0));
    emitWireVertex(1, vec3(0.0, 1.0, 0.0));
    emitWireVertex(2, vec3(0.0, 0.0, 1.0));
    EndPrimitive();
}

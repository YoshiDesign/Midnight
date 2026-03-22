#version 460

layout(location = 0) in vec3 inPosition; // from vertex buffer

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vWorldNormal;
layout(location = 2) out vec3 vMatWeights;
layout(location = 3) out float vSteep;

// Set 0: Per-frame global data
layout (std140, set = 0, binding = 1) uniform Matrices {
  mat4 view;
  mat4 projection;
};

// Set 1: Per-chunk compute outputs (SSBOs)
layout(std430, set = 1, binding = 0) readonly buffer VertexNormals {
    vec4 vertexNormals[]; // xyz = normal
};

layout(std430, set = 1, binding = 1) readonly buffer VertexWeights {
    vec4 weightsOut[]; // x=grass, y=rock, z=snow
};

layout(std430, set = 1, binding = 2) readonly buffer VertexSteepness {
    float steepnessOut[];
};

// Model transform via push constants (matches VkBasicTerrainDebugPC)
layout(push_constant) uniform PC {
    mat4 model;
} push;

void main()
{
    uint vid = uint(gl_VertexIndex);

    vec3 N = vertexNormals[vid].xyz;
    vec3 W = weightsOut[vid].xyz;

    vec4 worldPos4 = push.model * vec4(inPosition, 1.0);

    mat3 normalMat = mat3(push.model);
    vec3 worldN = normalize(normalMat * N);

    vWorldPos    = worldPos4.xyz;
    vWorldNormal = worldN;
    vMatWeights  = W;

    vSteep = steepnessOut[vid];

    gl_Position = projection * view * worldPos4;
}

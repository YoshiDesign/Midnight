#version 460

layout(location = 0) in vec3 inPosition; // from vertex buffer

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vWorldNormal;
layout(location = 2) out vec3 vMatWeights;
layout(location = 3) out float vSteep; // Extensible! We're not using this for much rn

// Matrices (use UBO or push constants; UBO is typical)
layout(set = 0, binding = 1, std140) uniform CameraUBO {
    mat4 viewProj;
} uCam;

// Model transform for this draw (chunk transform). If you already use push constants for this,
// swap this to push constants. Keeping it simple here.
layout(set = 0, binding = 2, std140) uniform ObjectUBO {
    mat4 model;
} uObj;

// Compute outputs (SSBOs) indexed by vertex ID
layout(std430, set = 0, binding = 3) readonly buffer VertexNormals {
    vec4 vertexNormals[]; // xyz = normal
};

layout(std430, set = 0, binding = 4) readonly buffer VertexWeights {
    vec4 weightsOut[]; // x=grass, y=rock, z=snow
};

layout(std430, set=0, binding=5) readonly buffer VertexSteepness {
    float steepnessOut[];
};

void main()
{
    // Vertex index *after* index-buffer fetch in Vulkan
    uint vid = uint(gl_VertexIndex);

    vec3 N = vertexNormals[vid].xyz;
    vec3 W = weightsOut[vid].xyz;

    vec4 worldPos4 = uObj.model * vec4(inPosition, 1.0);

    // If you have non-uniform scaling, normal transform needs inverse-transpose.
    // For terrain chunks with only translation / uniform scale, mat3(model) is fine.
    mat3 normalMat = mat3(uObj.model);
    vec3 worldN = normalize(normalMat * N);

    vWorldPos    = worldPos4.xyz;
    vWorldNormal = worldN;
    vMatWeights  = W;

    vSteep = steepnessOut[vid];

    gl_Position = uCam.viewProj * worldPos4;
}

#version 460 core
layout (location = 0) in vec4 aPos; // last float is uv.x
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec4 aNormal; // last float is uv.y
layout (location = 3) in uvec4 aBoneNum; // ignored
layout (location = 4) in vec4 aBoneWeight; // ignored

layout (location = 0) out vec4 color;
layout (location = 1) out vec4 normal;
layout (location = 2) out vec2 texCoord;
layout (location = 3) out vec3 fragPosWorld;
layout (location = 4) flat out uint vInstanceIndex;

layout (push_constant) uniform Constants {
  uint modelStride;
  uint instanceBaseIndex;
  uint skinMatrixOffset;
  uint pickId;
};

layout (std140, set = 1, binding = 0) uniform Matrices {
  mat4 view;
  mat4 projection;
};

layout (std430, set = 1, binding = 1) readonly restrict buffer WorldPosMatrices {
  mat4 worldPosMat[];
};

void main() {
  
  uint instanceId = gl_InstanceIndex + instanceBaseIndex;
  bool selected = (pickId == instanceId + 1);

  mat4 modelMat = worldPosMat[instanceId];
  gl_Position = projection * view * modelMat * vec4(aPos.x, aPos.y, aPos.z, 1.0);

  color = aColor;
  /* draw the instance always on top when highlighted, helps to find it better */
  if (selected) {
    gl_Position.z -= 1.0f;
  }

  vec4 positionWorld = modelMat * vec4(aPos.xyz, 1.0);

  // True normal transpose even when non-uniform
  // normal = transpose(inverse(modelMat)) * vec4(aNormal.x, aNormal.y, aNormal.z, 1.0);

  // Use mat3 instead of transpose(inverse()) - valid for uniform scale transforms
  normal = vec4(mat3(modelMat) * aNormal.xyz, 0.0);
  texCoord = vec2(aPos.w, aNormal.w);
  fragPosWorld = positionWorld.xyz;
  vInstanceIndex = gl_InstanceIndex;

}

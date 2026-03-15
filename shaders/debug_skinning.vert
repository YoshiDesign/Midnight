#version 460 core
layout (location = 0) in vec4 aPos; // last float is uv.x
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec4 aNormal; // last float is uv.y
layout (location = 3) in uvec4 aBoneNum;
layout (location = 4) in vec4 aBoneWeight;

layout (location = 0) out vec4 color;
layout (location = 1) out vec4 normal;
layout (location = 2) out vec2 texCoord;
layout (location = 3) out float selectInfo;
layout (location = 4) out vec3 fragPosWorld;
layout (location = 5) flat out uint vInstanceIndex;

layout (push_constant) uniform Constants {
  uint modelBoneStride;
  uint instanceBaseIndex;
  uint skinMatrixOffset;
  uint pickId;
};

layout (std140, set = 0, binding = 8) uniform Matrices {
  mat4 view;
  mat4 projection;
};

layout (std430, set = 0,  binding = 4) readonly restrict buffer BoneMatrices {
  mat4 boneMat[];
};

layout (std430, set = 0, binding = 5) readonly restrict buffer ModelMatrices {
  mat4 modelMat[];
};

void main() {

  uint instanceId = gl_InstanceIndex + instanceBaseIndex;
  bool selected = (pickId == instanceId + 1);

  uint skinMatOffset = gl_InstanceIndex * modelBoneStride + skinMatrixOffset;
  mat4 skinMat = 
    aBoneWeight.x * boneMat[aBoneNum.x + skinMatOffset] +
    aBoneWeight.y * boneMat[aBoneNum.y + skinMatOffset] +
    aBoneWeight.z * boneMat[aBoneNum.z + skinMatOffset] +
    aBoneWeight.w * boneMat[aBoneNum.w + skinMatOffset];

  mat4 worldPosSkinMat = modelMat[instanceId] * skinMat;
  vec4 positionWorld = worldPosSkinMat * vec4(aPos.xyz, 1.0f);
  gl_Position = projection * view * worldPosSkinMat * vec4(aPos.x, aPos.y, aPos.z, 1.0);

  color = aColor;

  /// vec4 worldPos = vec4(aPos.xyz, 1.0);    /// no model transform
  /// gl_Position = projection * view * worldPos;   ///

  /* draw the instance always on top when highlighted, helps to find it better */
  if (selected) {
    gl_Position.z -= 1.0f;
  }

  normal = transpose(inverse(worldPosSkinMat)) * vec4(aNormal.x, aNormal.y, aNormal.z, 1.0);
  texCoord = vec2(aPos.w, aNormal.w);
  fragPosWorld = positionWorld.xyz;
  vInstanceIndex = gl_InstanceIndex;

}

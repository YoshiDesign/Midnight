#version 450

// Per-vertex attributes (binding 0)
layout (location = 0) in vec4 position; // last float is uv.x!
layout (location = 1) in vec4 v_fragColor;
layout (location = 2) in vec4 normal; // last float is uv.y!
layout (location = 3) in uvec4 aBoneNum;
layout (location = 4) in vec4 aBoneWeight;


// Per-instance attributes (binding 1)
layout(location = 5) in mat4 instanceModelMatrix;     // locations 5,6,7,8
layout(location = 9) in mat4 instanceNormalMatrix;    // locations 9,10,11,12
layout(location = 13) in int instanceTextureIndex;    // location 13

layout (push_constant) uniform Constants {
  uint modelStride;
  uint worldPosOffset;
  uint skinMatrixOffset;
};

// Global uniform buffer
layout(set = 0, binding = 0) uniform GlobalUbo {
	mat4 projection;
	mat4 view;
	vec4 ambientLightColor; // w is intensity
	vec3 lightPosition;
	vec4 lightColor;
} ubo;

layout (std430, set = 1, binding = 1) readonly restrict buffer BoneMatrices {
  mat4 boneMat[];
};

layout (std430, set = 1, binding = 2) readonly restrict buffer WorldPosMatrices {
  mat4 worldPos[];
};

// Output to fragment shader
layout(location = 0) out vec3 f_fragColor;
layout(location = 1) out vec3 f_fragPosWorld;
layout(location = 2) out vec3 f_fragNormalWorld;
layout(location = 3) out vec2 f_fragTexCoord;
layout(location = 4) flat out int f_textureIndex;

void main() {
	uint skinMatOffset = gl_InstanceIndex * modelStride + skinMatrixOffset;

	mat4 skinMat =
		aBoneWeight.x * boneMat[aBoneNum.x + skinMatOffset] +
		aBoneWeight.y * boneMat[aBoneNum.y + skinMatOffset] +
		aBoneWeight.z * boneMat[aBoneNum.z + skinMatOffset] +
		aBoneWeight.w * boneMat[aBoneNum.w + skinMatOffset];

	mat4 worldPosSkinMat = worldPos[gl_InstanceIndex + worldPosOffset] * skinMat;
	vec4 fragPosWorld = worldPosSkinMat * vec4(position.xyz, 1.0);
	gl_Position = ubo.projection * ubo.view * worldPosSkinMat * vec4(position.x, position.y, position.z, 1.0);

	// Use instance normal matrix instead of push constants
	f_fragNormalWorld = normalize(mat3(instanceNormalMatrix) * normal.xyz);
	f_fragPosWorld    = fragPosWorld.xyz;
	f_fragColor		  = v_fragColor.xyz;
	f_fragTexCoord    = vec2(position.w, normal.w);
	f_textureIndex    = instanceTextureIndex;
}

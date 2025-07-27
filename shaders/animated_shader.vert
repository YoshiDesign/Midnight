#version 450

// AnimatedVertex input layout
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 v_fragColor;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec2 v_fragTexCoord;
layout(location = 4) in ivec4 boneIds;
layout(location = 5) in vec4 boneWeights;

layout(location = 0) out vec3 f_fragColor;
layout(location = 1) out vec3 f_fragPosWorld;
layout(location = 2) out vec3 f_fragNormalWorld;
layout(location = 3) out vec2 f_fragTexCoord;

// Global uniforms (Set 0)
layout(set = 0, binding = 0) uniform GlobalUbo {
	mat4 projection;
	mat4 view;
	vec4 ambientLightColor; // w is intensity
	vec3 lightPosition;
	vec4 lightColor;
} ubo;

// Animation uniforms (Set 3) 
layout(set = 3, binding = 1, std430) restrict readonly buffer BoneMatricesSSBO {
	mat4 boneMatrices[];
};

// Model matrix and normal matrix
layout(push_constant) uniform Push {
	mat4 modelMatrix;
	mat4 normalMatrix;
} push;

void main() {
	// Calculate skinned vertex position using bone weights
	vec4 skinnedPosition = vec4(0.0);
	vec3 skinnedNormal = vec3(0.0);
	
	// Apply bone transformations
	for (int i = 0; i < 4; i++) {
		if (boneIds[i] >= 0 && boneWeights[i] > 0.0) {
			mat4 boneTransform = boneMatrices[boneIds[i]];
			skinnedPosition += boneTransform * vec4(position, 1.0) * boneWeights[i];
			skinnedNormal += mat3(boneTransform) * normal * boneWeights[i];
		}
	}
	
	// If no valid bones, use original vertex data
	if (skinnedPosition.w == 0.0) {
		skinnedPosition = vec4(position, 1.0);
		skinnedNormal = normal;
	}
	
	// Transform to world space
	vec4 positionWorld = push.modelMatrix * skinnedPosition;
	gl_Position = ubo.projection * ubo.view * positionWorld;

	// Output interpolated data
	f_fragNormalWorld = normalize(mat3(push.normalMatrix) * skinnedNormal);
	f_fragPosWorld    = positionWorld.xyz;
	f_fragColor		  = v_fragColor;
	f_fragTexCoord    = v_fragTexCoord;
} 
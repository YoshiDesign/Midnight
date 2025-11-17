#version 450

// AnimatedVertex input layout - ALL vec4 for perfect alignment
layout(location = 0) in vec4 position;      // position.xyz + texCoord.x in .w
layout(location = 1) in vec4 v_fragColor;   // color.xyz + unused .w
layout(location = 2) in vec4 normal;        // normal.xyz + texCoord.y in .w
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

// Model matrix and bone offset for instance
layout(push_constant) uniform Push {
	mat4 modelMatrix;
	mat4 normalMatrix;
	int boneMatrixOffset;  // Offset into bone matrices buffer for this instance
} push;

void main() {
	// FIXED: Extract data from packed vec4 layout
	vec3 pos = position.xyz;         // Extract position
	vec3 norm = normal.xyz;          // Extract normal  
	vec3 color = v_fragColor.xyz;    // Extract color
	vec2 texCoord = vec2(position.w, normal.w);  // Extract UV from .w components
	
	// Use working example approach - blend bone matrices FIRST
	mat4 skinMatrix = mat4(0.0);
	float totalWeight = 0.0;
	
	// Calculate weighted bone matrix (like working example)
	for (int i = 0; i < 4; i++) {
		if (boneIds[i] >= 0 && boneWeights[i] > 0.0) {
			int boneIndex = boneIds[i] + push.boneMatrixOffset;
			skinMatrix += boneWeights[i] * boneMatrices[boneIndex];
			totalWeight += boneWeights[i];
		}
	}
	
	vec4 worldPosition;
	vec3 worldNormal;
	
	if (totalWeight > 0.0) {
		// Apply blended bone matrix to vertex ONCE
		vec4 skinnedLocalPos = skinMatrix * vec4(pos, 1.0);
		vec3 skinnedLocalNormal = mat3(skinMatrix) * norm;
		
		// Apply model matrix to skinned vertex (world transform)
		worldPosition = push.modelMatrix * skinnedLocalPos;
		worldNormal = mat3(push.normalMatrix) * skinnedLocalNormal;
	} else {
		// No bone influences - use original vertex with model matrix
		worldPosition = push.modelMatrix * vec4(pos, 1.0);
		worldNormal = mat3(push.normalMatrix) * norm;
	}
	
	// Final projection
	gl_Position = ubo.projection * ubo.view * worldPosition;

	// Output interpolated data
	f_fragNormalWorld = normalize(worldNormal);
	f_fragPosWorld    = worldPosition.xyz;
	f_fragColor		  = color;
	f_fragTexCoord    = texCoord;
} 
#version 450

// Per-vertex attributes (binding 0)
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 v_fragColor;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec2 v_fragTexCoord;

// Per-instance attributes (binding 1)
layout(location = 4) in mat4 instanceModelMatrix;     // locations 4, 5, 6, 7
layout(location = 8) in mat4 instanceNormalMatrix;    // locations 8, 9, 10, 11
layout(location = 12) in int instanceTextureIndex;    // location 12

// Output to fragment shader
layout(location = 0) out vec3 f_fragColor;
layout(location = 1) out vec3 f_fragPosWorld;
layout(location = 2) out vec3 f_fragNormalWorld;
layout(location = 3) out vec2 f_fragTexCoord;
layout(location = 4) flat out int f_textureIndex;

// Global uniform buffer
layout(set = 0, binding = 0) uniform GlobalUbo {
	mat4 projection;
	mat4 view;
	vec4 ambientLightColor; // w is intensity
	vec3 lightPosition;
	vec4 lightColor;
} ubo;

void main() {
	// Use instance model matrix instead of push constants
	vec4 positionWorld = instanceModelMatrix * vec4(position, 1.0);
	gl_Position = ubo.projection * ubo.view * positionWorld;

	// Use instance normal matrix instead of push constants
	f_fragNormalWorld = normalize(mat3(instanceNormalMatrix) * normal);
	f_fragPosWorld    = positionWorld.xyz;
	f_fragColor		  = v_fragColor;
	f_fragTexCoord    = v_fragTexCoord;
	f_textureIndex    = instanceTextureIndex;
}

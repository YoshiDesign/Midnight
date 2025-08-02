#version 450

// Vertex input for PRE-TRANSFORMED animated vertices (output from compute shader)
layout(location = 0) in vec3 position;      // Already transformed position
layout(location = 1) in vec3 v_fragColor;
layout(location = 2) in vec3 normal;        // Already transformed normal
layout(location = 3) in vec2 v_fragTexCoord;
// NOTE: No bone data needed - transformation already applied by compute shader

layout(location = 0) out vec3 f_fragColor;
layout(location = 1) out vec3 f_fragPosWorld;
layout(location = 2) out vec3 f_fragNormalWorld;
layout(location = 3) out vec2 f_fragTexCoord;

layout(set = 0, binding = 0) uniform GlobalUbo {
	mat4 projection;
	mat4 view;
	vec4 ambientLightColor; // w is intensity
	vec3 lightPosition;
	vec4 lightColor;
} ubo;

// Model matrix and normal matrix
layout(push_constant) uniform Push {
	mat4 modelMatrix;
	mat4 normalMatrix;
} push;

void main() {
	// Vertices are already transformed to world space by compute shader
	// Do NOT apply model matrix again to avoid double transformation
	gl_Position = ubo.projection * ubo.view * vec4(position, 1.0);

	f_fragNormalWorld = normalize(normal);  // Normal already transformed in compute shader
	f_fragPosWorld    = position;  // Position already in world space from compute shader
	f_fragColor		  = v_fragColor;
	f_fragTexCoord    = v_fragTexCoord;
} 
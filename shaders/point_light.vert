#version 450

const vec2 OFFSETS[6] = vec2[](
  vec2(-1.0, -1.0),
  vec2(-1.0, 1.0),
  vec2(1.0, -1.0),
  vec2(1.0, -1.0),
  vec2(-1.0, 1.0),
  vec2(1.0, 1.0)
);

layout (location = 0) out vec2 fragOffset;
layout (location = 1) out vec4 lightColor;

layout(set = 0, binding = 0) uniform GlobalUbo {
	mat4 projection;
	mat4 view;
	vec4 ambientLightColor; // w is intensity
	vec3 lightPosition;
	vec4 lightColor;
} ubo;

// Define the PointLight struct outside the uniform block
struct PointLight {
	vec4 position; // w is radius
	vec4 color;    // w is intensity
};

// Lights uniform buffer
layout(set = 1, binding = 0) uniform LightsUbo {
	uint numLights;
	PointLight lights[100];
} lightsUbo;

void main() {
	// Use instance index to select which light to render
	uint lightIndex = gl_InstanceIndex;
	
	if (lightIndex >= lightsUbo.numLights) {
		// Render off-screen if we're beyond the number of lights
		gl_Position = vec4(-2.0, -2.0, -2.0, 1.0);
		return;
	}

	PointLight light = lightsUbo.lights[lightIndex];
	float lightRadius = light.position.w;

	fragOffset = OFFSETS[gl_VertexIndex];
	lightColor = light.color;

	vec4 lightCameraSpace = ubo.view * vec4(light.position.xyz, 1.0);
	vec4 positionCameraSpace = lightCameraSpace + lightRadius * vec4(fragOffset, 0.0, 0.0);

	gl_Position = ubo.projection * positionCameraSpace;
}
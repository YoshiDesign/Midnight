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
} ubo;

// Lights uniform buffer - matches fragment shader layout
layout(set = 1, binding = 0) uniform LightsUbo {
	uint numLights;
	vec4 lightPositions[100];  // w component is radius
	vec4 lightColors[100];     // w component is intensity
} lightsUbo;

void main() {
	// Use instance index to select which light to render
	uint lightIndex = gl_InstanceIndex;
	
	if (lightIndex >= lightsUbo.numLights) {
		// Render off-screen if we're beyond the number of lights
		gl_Position = vec4(-2.0, -2.0, -2.0, 1.0);
		return;
	}

	vec4 lightPosition = lightsUbo.lightPositions[lightIndex];
	vec4 lightColorData = lightsUbo.lightColors[lightIndex];
	float lightRadius = lightPosition.w;

	fragOffset = OFFSETS[gl_VertexIndex];
	lightColor = lightColorData;

	vec4 lightCameraSpace = ubo.view * vec4(lightPosition.xyz, 1.0);
	vec4 positionCameraSpace = lightCameraSpace + lightRadius * vec4(fragOffset, 0.0, 0.0);

	gl_Position = ubo.projection * positionCameraSpace;
}
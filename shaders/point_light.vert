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

layout (std140, set = 0, binding = 8) uniform Matrices {
  mat4 view;
  mat4 projection;
};

layout(set = 0, binding = 6) uniform LightsUbo {
    vec4 ambientLightColor;
    vec4 lightPositions[200];  // w component is radius
    vec4 lightColors[200];     // w component is intensity
    uint numLights;
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

	vec4 lightCameraSpace = view * vec4(lightPosition.xyz, 1.0);
	vec4 positionCameraSpace = lightCameraSpace + lightRadius * vec4(fragOffset, 0.0, 0.0);

	gl_Position = projection * positionCameraSpace;
}
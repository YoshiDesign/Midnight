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

layout(set = 0, binding = 0) uniform GlobalUbo {
	mat4 projection;
	mat4 view;
	vec4 ambientLightColor; // w is intensity
	vec3 lightPosition;
	vec4 lightColor;
} ubo;

const float LIGHT_RADIUS = 0.561; // Can be a push constant?

void main() {

	fragOffset = OFFSETS[gl_VertexIndex]; // gl_VertexIndex contains the index of the current vertex being processed

	vec4 lightCameraSpace = ubo.view * vec4(ubo.lightPosition, 1.0);
	vec4 positionCameraSpace = lightCameraSpace + LIGHT_RADIUS * vec4(fragOffset, 0.0, 0.0);

	gl_Position = ubo.projection * positionCameraSpace;
}
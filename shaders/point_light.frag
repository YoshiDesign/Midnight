#version 450

layout(location=0) in vec2 fragOffset;
layout(location=1) in vec4 lightColor;
layout(location=0) out vec4 outColor;

layout(set = 0, binding = 0) uniform GlobalUbo {
	mat4 projection;
	mat4 view;
	vec4 ambientLightColor; // w is intensity
	vec3 lightPosition;
	vec4 lightColor;
} ubo;

void main() 
{
	float dis = sqrt(dot(fragOffset, fragOffset)); // calculate the distance this fragment is from the light's position (its origin)
	if (dis >= 1.0) {	// This effectively means anything beyond a 1 unit radius gets discarded, turning our square into a circle
		discard;	// fragment shader keyword
	}
	outColor = vec4(lightColor.xyz * lightColor.w, 1.0); // Use per-instance light color and intensity
}
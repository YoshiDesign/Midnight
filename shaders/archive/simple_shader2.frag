#version 450

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {

    // Gamma correction
    vec4 result = texture(texSampler, fragTexCoord);
    float gamma = 1.1;
    result.rgb = pow(result.rgb, vec3(1.0 / gamma));

    outColor = result;
}
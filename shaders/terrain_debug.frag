#version 460

layout(location = 0) in vec3 fragWorldPos;
layout(location = 0) out vec4 outColor;

void main()
{
    vec3 cyan    = vec3(0.0, 1.0, 1.0);
    vec3 magenta = vec3(1.0, 0.0, 1.0);

    float elevation = -fragWorldPos.y;
    float t = (elevation + 25.0) / 50.0;
    t = clamp(t, 0.0, 1.0);
    t = smoothstep(0.0, 1.0, t);

    outColor = vec4(mix(cyan, magenta, t), 1.0);
}
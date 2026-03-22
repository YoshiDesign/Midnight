#version 460

layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec3 fragWorldPos;

layout(set = 0, binding = 1) uniform CameraUBO
{
    mat4 view;
    mat4 proj;
} camera;

layout (push_constant) uniform Constants {
    mat4 modelMat;
};

void main()
{
    vec4 worldPos = vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    gl_Position = camera.proj * camera.view * worldPos;
}
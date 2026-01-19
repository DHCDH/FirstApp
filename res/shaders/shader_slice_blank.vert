#version 450

layout(location = 0) in vec3 inPosition;

layout(set = 0, binding = 0) uniform GlobalUbo {
    mat4 projection;
    mat4 view;
} ubo;

layout(push_constant) uniform Push {
    mat4 model;
    layout(offset = 64) vec3 normal;
    layout(offset = 80) vec3 point;
} push;

layout(location = 0) out vec3 vWorldPos;

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    vWorldPos = worldPos.xyz;
    gl_Position = ubo.projection * ubo.view * worldPos;
}
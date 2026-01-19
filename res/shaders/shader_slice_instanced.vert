#version 450

layout(location = 0) in vec3 inPosition;

layout(location = 4) in vec4 modelRow0;
layout(location = 5) in vec4 modelRow1;
layout(location = 6) in vec4 modelRow2;
layout(location = 7) in vec4 modelRow3;

layout(location = 0) out vec3 vWorldPos;

layout(set = 0, binding = 0) uniform GlobalUbo {
    mat4 projection;
    mat4 view;
} ubo;

layout(push_constant) uniform Push {
    mat4 unused;
    layout(offset = 64) vec3 normal;
    layout(offset = 80) vec3 point;
} push;

void main() {
    mat4 instanceModel = mat4(modelRow0, modelRow1, modelRow2, modelRow3);

    vec4 worldPos = instanceModel * vec4(inPosition, 1.0);
    vWorldPos = worldPos.xyz;

    gl_Position = ubo.projection * ubo.view * worldPos;
}
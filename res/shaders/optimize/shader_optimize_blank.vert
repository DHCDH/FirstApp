#version 450

#extension GL_ARB_shader_viewport_layer_array : require

layout(location = 0) in vec3 inPosition;

struct CameraData { 
    mat4 projView;
    vec4 mapInfo;
};
layout(std430, set = 0, binding = 0) readonly buffer CameraSSBO {
    CameraData cameras[];
}cameraData;

layout(push_constant) uniform Push {
    mat4 model;
    layout(offset = 64) vec3 normal;
    layout(offset = 80) vec3 point;
} push;

layout(location = 0) out vec3 vWorldPos;

void main() {
    mat4 myCamera = cameraData.cameras[gl_InstanceIndex].projView;

    vWorldPos = (push.model * vec4(inPosition, 1.)).xyz;

    gl_Position = myCamera * push.model * vec4(inPosition, 1.);
    gl_Layer = gl_InstanceIndex;
}
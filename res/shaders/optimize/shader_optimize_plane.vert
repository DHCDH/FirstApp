#version 450

#extension GL_ARB_shader_viewport_layer_array : require

layout(location = 0) out vec4 vClipPos;

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

void main()
{
    // 生成覆盖全屏的三角形
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec4 pos = vec4(uv * 2.0f - 1.0f, 0.0f, 1.0f);

    gl_Position = pos;

    // 构造一个位于yM高度的世界坐标点
    vec4 worldPosOnPlane = vec4(push.point, 1.);

    // 计算点在裁剪空间的坐标
    mat4 myCamera = cameraData.cameras[gl_InstanceIndex].projView;
    vClipPos = myCamera * worldPosOnPlane;

    gl_Layer = gl_InstanceIndex;
}
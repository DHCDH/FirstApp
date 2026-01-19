#version 450

layout(location = 0) out vec4 vClipPos;

layout(set = 0, binding = 0) uniform GlobalUbo {
    mat4 projection;
    mat4 view;
} ubo;

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
    vClipPos = ubo.projection * ubo.view * worldPosOnPlane;
}
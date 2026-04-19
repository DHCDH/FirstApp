#version 450
layout(location = 0) out vec4 outColor;
layout(push_constant) uniform Push {
    vec4 color; // 我们直接通过 PushConstant 把算好的颜色传进来
} push;

void main() {
    outColor = push.color;
}
#version 450

layout(location = 0) in vec3 vWorldPos;

layout(location = 0) out uint outMask;

layout(push_constant) uniform Push {
    mat4 model;
    layout(offset = 64) vec3 normal;
    layout(offset = 80) vec3 point;
} push;

void main() {

    // 当前正在渲染的像素点，距离设定的截面的物理距离
    float dis = dot(vWorldPos - push.point, push.normal);
    
    // 像素点位于截面前方，则丢弃
    if(dis > 0.) {
        discard;
    }
    
    outMask = 1u;
}
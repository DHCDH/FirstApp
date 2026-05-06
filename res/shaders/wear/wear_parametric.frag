#version 450

layout(location = 0) in vec3 vWorldPos;

layout(location = 0) out uint outMask;

layout(push_constant) uniform Push {
    layout(offset = 0)  vec3 normal; 
    layout(offset = 16) vec4 grVec;  
    layout(offset = 32) vec3 point;  
    layout(offset = 48) vec4 gaVec;  
    layout(offset = 64) float gR;    
    layout(offset = 80) float width;
} push;

void main() {
    // 经典的半空间剔除逻辑
    // 如果像素位于切片法向的一侧，则丢弃它，形成截面
    float dis = dot(vWorldPos - push.point, push.normal);

    if(dis > 0.) {
        discard;
    }

    outMask = 2u;
}
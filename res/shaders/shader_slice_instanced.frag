#version 450

layout(location = 0) in vec3 vWorldPos;

layout(location = 0) out uint outMask;

layout(push_constant) uniform Push {
    mat4 unused;    // 占位
    float yM;
    float thickness;
} push;

void main() {
    float dy = abs(vWorldPos.y - push.yM);
    if(dy > push.thickness * 0.5) {
        discard;
    }
    outMask = 2u;
}
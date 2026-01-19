#version 450

layout(location = 0) in vec3 vWorldPos;

layout(location = 0) out uint outMask;

layout(push_constant) uniform Push {
    mat4 unused;
    layout(offset = 64) vec3 normal;
    layout(offset = 80) vec3 point;
} push;

void main() {
    float dis = dot(vWorldPos - push.point, push.normal);

    if(dis > 0.) {
        discard;
    }

    outMask = 2u;
}
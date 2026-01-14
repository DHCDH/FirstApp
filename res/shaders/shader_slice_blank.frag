#version 450

layout(location = 0) in vec3 vWorldPos;

layout(location = 0) out uint outMask;

layout(push_constant) uniform Push {
    mat4 model;
    float xM;
    float thickness;
} push;

void main() {
    if (vWorldPos.x < push.xM) {
        discard;
    }
    outMask = 1u;
}
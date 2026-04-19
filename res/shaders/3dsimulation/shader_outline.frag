#version 450

layout(location = 0) in vec3 fragPosWorld;
layout(location = 1) in vec3 fragNormalWorld;

layout(location = 0) out vec4 outColor;

struct PointLight {
    vec4 position;
    vec4 color;
};

// 必须与FirstApp.cpp 中的 GlobalUbo 严格对应
layout(set = 0, binding = 0) uniform GlobalUbo {
    mat4 projection;
    mat4 view;
    mat4 invView;
    vec4 ambientLightColor;
    PointLight pointLights[20];
    int numLights;

    // 自定义视点剔除
    vec4 obsCamPos;
    int useCustomCulling;
} ubo;

layout(set = 2, binding = 0, std140) uniform UMaterial {
    vec4 baseColorFactor;
    vec4 uvTilingOffset;
    vec4 pbrAoAlpha;
    uvec4 flags;
} matu;

void main() {

    // 纯黑色，完全不透明
    outColor = vec4(0.0, 0.0, 0.0, 1.0); 
}
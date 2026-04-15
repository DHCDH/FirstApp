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

void main() {

    // 和主着色器一样的自定义剔除逻辑
    if (ubo.useCustomCulling == 1) {
        vec3 dirToObsCam = normalize(ubo.obsCamPos.xyz - fragPosWorld);
        float dotResult = dot(normalize(fragNormalWorld), dirToObsCam);
        
        if (dotResult <= 0.0) {
            discard; // 丢弃背对相机的描边像素！
        }
    }

    // 纯黑色，完全不透明
    outColor = vec4(0.0, 0.0, 0.0, 1.0); 
}
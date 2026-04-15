#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec2 uv;

layout(location = 0) out vec3 fragPosWorld;
layout(location = 1) out vec3 fragNormalWorld;

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

layout(push_constant) uniform Push {
    mat4 modelMatrix;
    mat4 normalMatrix;
} push;

void main() {
    // 这里的 0.1 是描边粗细，你需要根据砂轮的实际尺寸适当调大或调小
    float outlineThickness = 0.1;
    
    // 让顶点沿着法线方向向外膨胀
    vec3 inflatedPos = position + normal * outlineThickness;

    // 计算世界坐标和世界法线，传给 FS
    vec4 positionWorld = push.modelMatrix * vec4(inflatedPos, 1.0);
    fragPosWorld = positionWorld.xyz;
    fragNormalWorld = normalize(mat3(push.normalMatrix) * normal);
    
    gl_Position = ubo.projection * ubo.view * push.modelMatrix * vec4(inflatedPos, 1.0);
}
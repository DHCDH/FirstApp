#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec2 uv;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragPosWorld;    // 顶点世界位置
layout(location = 2) out vec3 fragNormalWorld;    //片段中的法线
layout(location = 3) out vec2 fragUV;

struct PointLight {
    vec4 position;  // ignore w
    vec4 color;     // w is intensity
};

layout(set = 0, binding = 0) uniform GlobalUbo {
    mat4 projection;
    mat4 view;
    mat4 invView;
    vec4 ambientLightColor;
    PointLight pointLights[10]; //应使用特化常量而非硬编码
    int numLights;
}ubo;

layout(push_constant) uniform Push {
    mat4 modelMatrix;
    mat4 normalMatrix;
} push;

void main() {
    vec4 positionWorld = push.modelMatrix * vec4(position, 1.0);    // 先将顶点从模型坐标系转换到世界坐标系，再计算光源方向

    gl_Position = ubo.projection * ubo.view * push.modelMatrix * vec4(position, 1.0);

    fragNormalWorld = normalize(mat3(push.normalMatrix) * normal);
    fragPosWorld = positionWorld.xyz;
    fragColor = color;
    fragUV = uv;
}
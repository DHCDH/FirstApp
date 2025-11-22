#version 450

// 顶点（binding = 0）
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV;

// 实例矩阵四行（binding = 1）
layout(location = 4) in vec4 inInstanceRow0;
layout(location = 5) in vec4 inInstanceRow1;
layout(location = 6) in vec4 inInstanceRow2;
layout(location = 7) in vec4 inInstanceRow3;

// 输出给 fragment shader
layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragPosWorld;
layout(location = 2) out vec3 fragNormalWorld;
layout(location = 3) out vec2 fragUV;
layout(location = 4) flat out int useFragColor;

// 和原来 shader.vert 一致的结构/UBO（名字保持一致）
struct PointLight {
    vec4 position;   // xyz 位置, w 忽略
    vec4 color;      // rgb 颜色, w 强度
};

layout(set = 0, binding = 0) uniform GlobalUbo {
    mat4 projection;
    mat4 view;
    mat4 invView;
    vec4 ambientLightColor;
    PointLight pointLights[20];
    int numLights;
} ubo;

void main()
{
    // 从四个 vec4 还原出实例的 model 矩阵
    mat4 instanceModel = mat4(
        inInstanceRow0,
        inInstanceRow1,
        inInstanceRow2,
        inInstanceRow3
    );

    // 模型空间 -> 世界空间
    vec4 worldPos = instanceModel * vec4(inPosition, 1.0);

    fragPosWorld    = worldPos.xyz;
    fragNormalWorld = normalize(mat3(instanceModel) * inNormal);
    // fragColor       = inColor;
    fragColor       = vec3(1.f, 0.f, 0.f);
    fragUV          = inUV;

    gl_Position = ubo.projection * ubo.view * worldPos;

    useFragColor = 1;
}


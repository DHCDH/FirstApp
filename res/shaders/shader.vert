#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec2 uv;

layout(location = 0) out vec3 fragColor;

layout(set = 0, binding = 0) uniform GlobalUbo {
    mat4 projectionViewMatrix;
    vec4 ambientLightColor;
    vec3 lightPosition;
    vec4 lightColor;
}ubo;

layout(push_constant) uniform Push {
    mat4 modelMatrix;
    mat4 normalMatrix;
} push;

void main() {
    vec4 positionWorld = push.modelMatrix * vec4(position, 1.0);    // 先将顶点从模型坐标系转换到世界坐标系，再计算光源方向

    gl_Position = ubo.projectionViewMatrix * push.modelMatrix * vec4(position, 1.0);

    vec3 normalWorldSpace = normalize(mat3(push.normalMatrix) * normal);

    vec3 directionToLight = ubo.lightPosition - positionWorld.xyz;  // 计算点光源方向
    float attenuation = 1.0 / dot(directionToLight, directionToLight);  // 衰减因子，与光源距离平方成反比

    vec3 lightColor = ubo.lightColor.xyz * ubo.lightColor.w * attenuation; // 根据点光源强度来缩放其颜色
    vec3 ambientLight = ubo.ambientLightColor.xyz * ubo.ambientLightColor.w;

    vec3 diffuseLight = lightColor * max(dot(normalWorldSpace, normalize(directionToLight)), 0.0);

    fragColor = (diffuseLight + ambientLight) * color;
}
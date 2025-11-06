#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragPosWorld;
layout(location = 2) in vec3 fragNormalWorld;

layout(location = 0) out vec4 outColor;	// 输出到颜色附件的第0个位置

struct PointLight {
    vec4 position;  // ignore w
    vec4 color;     // w is intensity
};

layout(set = 0, binding = 0) uniform GlobalUbo {
    mat4 projection;
    mat4 view;
    vec4 ambientLightColor;
    PointLight pointLights[10]; //应使用特化常量而非硬编码
    int numLights;
}ubo;


layout(push_constant) uniform Push {
    mat4 modelMatrix;
    mat4 normalMatrix;
} push;

void main() {
    vec3 diffuseLight = ubo.ambientLightColor.xyz * ubo.ambientLightColor.w;
    vec3 surfaceNormal = normalize(fragNormalWorld);

    /*遍历所有点光源*/
    for(int i = 0; i < ubo.numLights; i++) {
        /*计算每个光源对总漫反射的贡献*/
        PointLight light = ubo.pointLights[i];
        vec3 directionToLight = light.position.xyz - fragPosWorld;  // 计算点光源方向
        float attenuation = 1.0 / dot(directionToLight, directionToLight);  // 衰减因子，与光源距离平方成反比
        float cosAngIncidence = max(dot(surfaceNormal, normalize(directionToLight)), 0.0);
        vec3 intensity = light.color.xyz * light.color.w * attenuation; // 根据点光源强度来缩放其颜色

        diffuseLight += intensity * cosAngIncidence;
    }

	outColor = vec4(diffuseLight * fragColor, 1.0);
}
#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragPosWorld;
layout(location = 2) in vec3 fragNormalWorld;

layout(location = 0) out vec4 outColor;	// 输出到颜色附件的第0个位置

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
    vec3 directionToLight = ubo.lightPosition - fragPosWorld;  // 计算点光源方向
    float attenuation = 1.0 / dot(directionToLight, directionToLight);  // 衰减因子，与光源距离平方成反比

    vec3 lightColor = ubo.lightColor.xyz * ubo.lightColor.w * attenuation; // 根据点光源强度来缩放其颜色
    vec3 ambientLight = ubo.ambientLightColor.xyz * ubo.ambientLightColor.w;

    vec3 diffuseLight = lightColor * max(dot(normalize(fragNormalWorld), normalize(directionToLight)), 0.0);

	outColor = vec4((diffuseLight + ambientLight) * fragColor, 1.0);
}
#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragPosWorld;
layout(location = 2) in vec3 fragNormalWorld;
layout(location = 3) in vec2 fragUV;

layout(location = 0) out vec4 outColor;	// 输出到颜色附件的第0个位置

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

layout(set = 0, binding = 1) uniform sampler2D uTexture;

void main() {
    vec3 ambient = ubo.ambientLightColor.rgb * ubo.ambientLightColor.a;
    // vec3 diffuseLight = ubo.ambientLightColor.xyz * ubo.ambientLightColor.w;
    // vec3 specularLight = vec3(0.0); // 存储每个点光源对镜面反射的贡献
    vec3 diffuseLight  = vec3(0.0);
    vec3 specularLight = vec3(0.0);
    vec3 surfaceNormal = normalize(fragNormalWorld);

    vec3 cameraPosWorld = ubo.invView[3].xyz;   // 逆视图矩阵最后一列为相机在世界空间的位置
    vec3 viewDirection = normalize(cameraPosWorld - fragPosWorld);  // 指向观察者的表面向量

    /*遍历所有点光源*/
    for(int i = 0; i < ubo.numLights; i++) {
        /*计算每个光源对总漫反射的贡献*/
        PointLight light = ubo.pointLights[i];
        vec3 directionToLight = light.position.xyz - fragPosWorld;  // 计算点光源方向
        float attenuation = 1.0 / dot(directionToLight, directionToLight);  // 衰减因子，与光源距离平方成反比
        directionToLight = normalize(directionToLight);

        float cosAngIncidence = max(dot(surfaceNormal, directionToLight), 0.0);
        vec3 intensity = light.color.xyz * light.color.w * attenuation; // 根据点光源强度来缩放其颜色

        diffuseLight += intensity * cosAngIncidence;

        // 计算镜面反射
        vec3 halfAngle = normalize(directionToLight + viewDirection);
        float blinnTerm = dot(surfaceNormal, halfAngle);
        blinnTerm = clamp(blinnTerm, 0, 1);
        blinnTerm = pow(blinnTerm, 32.0); // higher values -> sharper highlights
        // specularLight = light.color.xyz * intensity * blinnTerm;
        specularLight += intensity * blinnTerm; 
    }

    /*采样纹理*/
    vec3 albedo = texture(uTexture, fragUV).rgb;
    vec3 base = albedo * fragColor;

    vec3 lit = ambient * base + diffuseLight * base + specularLight;

	outColor = vec4(lit, 1.0);
}
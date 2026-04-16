#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragPosWorld;
layout(location = 2) in vec3 fragNormalWorld;
layout(location = 3) in vec2 fragUV;
layout(location = 4) flat in int useFragColor;

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

layout(set = 1, binding = 0) uniform sampler2D uAlbedo;

// 必须与你的 FirstApp.cpp 中的 MaterialUBO 严格对应
layout(set = 2, binding = 0, std140) uniform UMaterial {
    vec4 baseColorFactor;
    vec4 uvTilingOffset;
    vec4 pbrAoAlpha;
    uvec4 flags;
} matu;

layout(push_constant) uniform Push {
    mat4 modelMatrix;
    mat4 normalMatrix;
} push;

void main() 
{
    // --- 自定义视点剔除 ---
    if(ubo.useCustomCulling == 1) {
        // --- 开启背面/正面剔除 ---
        // 计算当前像素指向摄像机1的向量
        vec3 dirToObsCam = normalize(ubo.obsCamPos.xyz - fragPosWorld);

        // 计算法线与该向量的点乘
        float dotResult = dot(normalize(fragNormalWorld), dirToObsCam);

        // 如果点乘 <= 0，说明该面背对着摄像机1，直接丢弃！
        if (dotResult <= 0.0) {
            discard; 
        }

        // 剔除位于平面上方/下方的点
        // if(fragPosWorld.y < 0.) {
        //     discard;
        // }
    }


    // 1. 强制使用纯色作为基础色 (忽略贴图，呈现干净的 CAD 质感)
    vec3 baseColor = matu.baseColorFactor.rgb;

    // 2. 计算环境光底色
    vec3 ambient = baseColor * ubo.ambientLightColor.rgb * ubo.ambientLightColor.a;

    if(matu.flags.y == 1u) {
        ambient = baseColor * 0.85;
    }

    // 3. 准备光照向量
    vec3 N = normalize(fragNormalWorld);
    vec3 camPos = ubo.invView[3].xyz; // 从视图矩阵逆推相机位置
    vec3 V = normalize(camPos - fragPosWorld);

    vec3 diffuse = vec3(0.0);
    vec3 specular = vec3(0.0);

    // // 4. 累加所有点光源的光照
    // int n = ubo.numLights;
    // for (int i = 0; i < n; ++i) {
    //     vec3 Lpos = ubo.pointLights[i].position.xyz;
    //     vec3 L = normalize(Lpos - fragPosWorld);
        
    //     // 光源衰减
    //     // float dist = length(Lpos - fragPosWorld);
    //     // float att = 1.0 / max(dist * dist, 0.001);
    //     // vec3 lightColor = ubo.pointLights[i].color.rgb * ubo.pointLights[i].color.a * att;

    //     // 取消光源衰减，改为恒定亮度
    //     vec3 lightColor = ubo.pointLights[i].color.rgb * ubo.pointLights[i].color.a;

    //     // 漫反射 (Diffuse)
    //     float ndl = max(dot(N, L), 0.0);
    //     diffuse += baseColor * lightColor * ndl;

    //     // 高光 (Specular - 呈现金属/塑料光泽)
    //     vec3 H = normalize(L + V);
    //     float specFactor = pow(max(dot(N, H), 0.0), 256.0); // 256是高光锐度
    //     float specIntensity = gl_FrontFacing ? 0.5 : 0.15;
    //     // specular += vec3(0.5) * lightColor * specFactor;   // 0.5控制高光反光强度

    //     if(matu.flags.y == 1u) {
    //         specIntensity = 0.;
    //     }

    //     specular += vec3(specIntensity) * lightColor * specFactor;
    // }

    // 4. 累加所有点光源的光照
    int n = ubo.numLights;
    for (int i = 0; i < n; ++i) {
        vec3 Lpos = ubo.pointLights[i].position.xyz;
        vec3 L = normalize(Lpos - fragPosWorld);
        
        // 取消光源衰减，改为恒定亮度
        vec3 lightColor = ubo.pointLights[i].color.rgb * ubo.pointLights[i].color.a;

        // ==========================================
        // 漫反射 (Diffuse)
        // ==========================================
        // 使用 abs() 确保砂轮被切开的内壁也能受光
        float ndl = max(abs(dot(N, L)), 0.1); 
        vec3 surfaceDiffuse = baseColor * lightColor * ndl;

        // 【特权 1】：平面已经有了极高的环境光，不需要漫反射叠加，防止正视时过曝发白！
        if (matu.flags.y == 1u) {
            surfaceDiffuse = vec3(0.0);
        }
        // 【特权 2】：砂轮的内壁（被丢弃切开的部分）强行压暗 40%，增加清晰的折角立体感
        else if (!gl_FrontFacing) {
            surfaceDiffuse *= 0.6; 
        }

        diffuse += surfaceDiffuse;

        // ==========================================
        // 高光 (Specular - 呈现金属/塑料光泽)
        // ==========================================
        vec3 H = normalize(L + V);
        float specFactor = pow(max(dot(N, H), 0.0), 256.0); 
        float specIntensity = gl_FrontFacing ? 0.5 : 0.15;
        
        // 【特权 3】：平面不需要高光，保持纯净底色
        if(matu.flags.y == 1u) {
            specIntensity = 0.0;
        }

        specular += vec3(specIntensity) * lightColor * specFactor;
    }

    // 5. 合成最终光照
    vec3 finalColor = ambient + diffuse + specular;

    // 6. HDR 色调映射 (ACES 拟合或简单的 Reinhard) -> 防止白底光照过曝
    // finalColor = finalColor / (finalColor + vec3(1.0));
    
    // 7. Gamma 校正 -> 让暗部细节更清晰
    finalColor = pow(finalColor, vec3(1.0 / 2.2));

    outColor = vec4(finalColor, matu.baseColorFactor.a);
}
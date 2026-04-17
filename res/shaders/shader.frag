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
    // ==========================================
    // 0. 提前准备基础向量 (必须放在最前面，修复编译错误！)
    // ==========================================
    vec3 N = normalize(fragNormalWorld);
    vec3 camPos = ubo.invView[3].xyz; 
    vec3 V = normalize(camPos - fragPosWorld);

    // 获取基础色
    // vec3 baseColor = matu.baseColorFactor.rgb;
    vec3 baseColor = pow(matu.baseColorFactor.rgb, vec3(2.2));

    // ==========================================
    // 1. 自定义视点剔除 (剖面生成)
    if(ubo.useCustomCulling == 1) {

        // --- 砂轮 ---
        if(matu.flags.x == 0u) {
            vec3 dirToObsCam = normalize(ubo.obsCamPos.xyz - fragPosWorld);
            float dotResult = dot(normalize(fragNormalWorld), dirToObsCam);

            // 背面颜色加深
            if (dotResult <= 0.0) {
                // baseColor *= 0.3;
                vec3 srgb = vec3(1., 0.2706, 0.);
                baseColor = pow(srgb, vec3(2.2)) * 0.8;
            }

            // 背面剔除
            // if (dotResult <= 0.0) {
            //     discard;
            // }

            // 剔除 Y 轴坐标小于 0 的面片
            // if(fragPosWorld.x > 0.0) {
            //     discard;
            // }
        }

        // --- 平面 ---
        if(matu.flags.x == 1u) {
            if(matu.flags.z == 1u) {
                // --- 平面网格化 ---
                // 1. 网格参数配置
                float cells = 10.0;     // 把平面精确地划分为 10x10 的方格
                float thickness = 0.03; // 内部网格线的粗细比例

                float uvThickness = thickness / cells;
                
                // 2. 【核心修改】：使用 fragUV (0.0~1.0) 代替 fragPosWorld
                vec2 grid = fract(fragUV * cells);
                
                // 3. 判定当前像素是不是网格线
                bool isGridLine = (grid.x < thickness || grid.y < thickness);
                
                // 4. 【高阶细节】：给平面的最外侧加上一圈闭合边框，防止格子在边缘漏气
                // 0.01 是外边框的粗细，可以根据你的喜好微调
                bool isBorder = (fragUV.x < uvThickness || fragUV.x > 1 - uvThickness || 
                                fragUV.y < uvThickness || fragUV.y >1 - uvThickness);

                // 5. 上色
                if (isBorder || isGridLine) {
                    outColor = vec4(baseColor, 0.8);  // 边框和网格线用深色、不透明
                } else {
                    // outColor = vec4(baseColor, 0.15); // 格子内部用极浅的玻璃底色
                    discard;
                }
                
                // 渲染完这块“标定板”直接返回！
                return;
            }
        }

        // --- 立方体 ---
        if(matu.flags.x == 2u) {
            vec3 localPos = abs((inverse(push.modelMatrix) * vec4(fragPosWorld, 1.0)).xyz);
            float maxVal = max(localPos.x, max(localPos.y, localPos.z));
            vec3 normPos = localPos / maxVal;
            
            float thickness = 0.002; // 线框基础粗细
            
            // 1. 【抗锯齿核心】：fwidth 会计算当前像素与相邻像素在 normPos 上的差值
            // 它能自适应你相机的远近，保证不管多远多近，边缘过渡永远是精确的 1 个像素宽！
            vec3 fw = fwidth(normPos); 
            
            // 2. 使用 smoothstep 将生硬的边界变为平滑的渐变
            // 结果 edgeMask 的 x,y,z 值会在面边缘处极其平滑地从 0.0 涨到 1.0
            vec3 edgeMask = smoothstep(1.0 - thickness - fw, 1.0 - thickness + fw, normPos);
            
            // 3. 判定 3D 边框：如果处于两个或三个面的交界处，把它们乘起来
            float edgeFactor = edgeMask.x * edgeMask.y + 
                               edgeMask.y * edgeMask.z + 
                               edgeMask.z * edgeMask.x;
            
            // 限制最大值为 1.0
            edgeFactor = clamp(edgeFactor, 0.0, 1.0);
            
            // 4. 彻底掏空内部，节省性能
            if (edgeFactor < 0.01) {
                discard; 
            }
            
            // 5. 【施展魔法】：把算出来的平滑因子 edgeFactor 乘到透明度 (Alpha) 上！
            // 这样线框边缘就会有微弱的半透明过渡，GPU 的混合器会自动把它变平滑。
            outColor = vec4(baseColor, matu.baseColorFactor.a * edgeFactor);
            
            return;
        }
    }

    // ==========================================
    // 2. 高级 CAD 环境光 (Hemisphere Lighting)
    // ==========================================
    vec3 ambient;
    
    if (matu.flags.x == 1u && matu.flags.y == 1u) {
        // 平面保持特权恒定发光
        ambient = baseColor * 0.85; 
    } else {
        // 【核心科技】：半球环境光
        // 根据法线朝世界上方(Y轴)的程度，给予不同的底光。
        // 这能保证相互垂直的两个面，即使都在阴影中，亮度也绝对不同！
        float hemi = dot(N, vec3(0.0, 1.0, 0.0)) * 0.5 + 0.5; // 结果映射到 0.0 ~ 1.0
        ambient = baseColor * (0.15 + 0.4 * hemi); 
    }

    // ==========================================
    // 3. 光照累加 (漫反射 + 高光)
    // ==========================================
    vec3 diffuse = vec3(0.0);
    vec3 specular = vec3(0.0);

    int n = ubo.numLights;
    for (int i = 0; i < n; ++i) {
        vec3 Lpos = ubo.pointLights[i].position.xyz;
        vec3 L = normalize(Lpos - fragPosWorld);
        vec3 lightColor = ubo.pointLights[i].color.rgb * ubo.pointLights[i].color.a;

        // 漫反射
        float ndl = max(abs(dot(N, L)), 0.1); 
        vec3 surfaceDiffuse = baseColor * lightColor * ndl;

        if (matu.flags.x == 1u && matu.flags.y == 1u) {
            surfaceDiffuse = vec3(0.0); // 平面免除漫反射叠加
        } else if (!gl_FrontFacing) {
            surfaceDiffuse *= 0.6; // 内壁压暗
        }

        diffuse += surfaceDiffuse;

        // 高光
        vec3 H = normalize(L + V);
        float specFactor = pow(max(dot(N, H), 0.0), 256.0); 
        float specIntensity = gl_FrontFacing ? 0.4 : 0.1;
        
        if(matu.flags.x == 1u && matu.flags.y == 1u) {
            specIntensity = 0.0; // 平面去除高光
        }
        specular += vec3(specIntensity) * lightColor * specFactor; 
    }

    // ==========================================
    // 4. 合成最终输出
    // ==========================================
    vec3 finalColor = ambient + diffuse + specular;
    
    // Gamma 校正
    finalColor = pow(finalColor, vec3(1.0 / 2.2));

    outColor = vec4(finalColor, matu.baseColorFactor.a);
}
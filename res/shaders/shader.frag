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

// 3D 空间伪随机哈希函数（映射到 -1.0 ~ 1.0）
vec3 hash3(vec3 p) {
    vec3 q = vec3( dot(p,vec3(127.1,311.7, 74.7)),
                   dot(p,vec3(269.5,183.3,246.1)),
                   dot(p,vec3(113.5,271.9,124.6)));
    return fract(sin(q)*43758.5453) * 2.0 - 1.0;
}

void main() 
{
    // ==========================================
    // 0. 提前准备基础向量 (必须放在最前面，修复编译错误！)
    // ==========================================
    vec3 N = normalize(fragNormalWorld);
    vec3 camPos = ubo.invView[3].xyz; 
    vec3 V = normalize(camPos - fragPosWorld);

    if(matu.flags.x == 0u) {
        // 获取砂轮的局部坐标 (保证砂轮旋转时颗粒跟着转)
        vec3 localPos = (inverse(push.modelMatrix) * vec4(fragPosWorld, 1.0)).xyz;

        // 生成高频法线扰动。
        // [参数调节]：500.0 是颗粒的密度(频率)。值越大，砂轮颗粒越细；值越小，颗粒越粗(像碎石)。
        vec3 normalJitter = hash3(localPos * 1500.0);

        // 将噪点叠加到原本完美的法线上。
        // [参数调节]：0.15 是凹凸的强度。值越大表面越粗糙、暗淡；值越小越接近原本的光滑。
        N = normalize(N + normalJitter * 0.04);
    }

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

            float objCenterX = push.modelMatrix[3].x;
            float distanceMoved = abs(objCenterX - (-0.642588));
            float trackFactor = clamp(distanceMoved / 10, 0.0, 1.0);
            vec3 frontStart = vec3(0.05, 0.15, 0.35); // 第 1 个砂轮的颜色：红
            vec3 frontEnd   = vec3(0.12, 0.45, 0.85); // 最后 1 个砂轮的颜色：蓝
            baseColor = pow(mix(frontStart, frontEnd, trackFactor), vec3(2.2));

            // 背面颜色加深
            if (dotResult <= 0.0) {
                // vec3 srgb = vec3(1.00, 0.45, 0.15);
                // baseColor = pow(srgb, vec3(2.2)) * 0.8;
                vec3 backStart = vec3(0.149, 0.1765, 0.2392); // 第 1 个砂轮的剖面色：明黄
                vec3 backEnd   = vec3(0.3608, 0.4078, 0.5176); // 最后 1 个砂轮的剖面色：翠绿
                baseColor = pow(mix(backStart, backEnd, trackFactor), vec3(2.2)) * 0.9;
            }

            // 正面剔除
            if (dotResult > 0.0) {
                discard;
            }

            // 剔除 X 轴坐标大于 0 的面片
            if(fragPosWorld.x > 0.0) {
                discard;
            }
        }

        // --- 平面 ---
        if(matu.flags.x == 1u) {
            if(matu.flags.z == 1u) {
                // --- 平面网格化 ---
                float cells = 10.0;
                float thickness = 0.03;

                // 1. 将 UV 坐标分块，并折叠到中心
                vec2 uv = fragUV * cells;
                // 计算当前点到最近网格线的距离 (0.0 表示在网格正中央，1.0 表示在网格线上)
                vec2 dist = abs(fract(uv) - 0.5) * 2.0; 

                // 2. 屏幕空间导数
                // fwidth 能知道相邻像素在 UV 空间里跨度有多大
                vec2 fw = fwidth(uv) * 2.0; 

                // 3. 防止断线
                // 如果摄像机太远，线宽小于了屏幕上的 1 个像素 (fw/2.0)，就强行把线撑宽到 1 个像素！
                vec2 renderThickness = max(vec2(thickness), fw / 2.0); 

                // 4. 平滑抗锯齿
                // 使用 smoothstep 在线的边缘制造半透明过渡
                vec2 edgeMask = smoothstep(1.0 - renderThickness - fw, 1.0 - renderThickness + fw, dist);
                float gridFactor = max(edgeMask.x, edgeMask.y);

                // --- 边框处理（原理同上） ---
                vec2 uvDist = abs(fragUV - 0.5) * 2.0; 
                vec2 borderFw = fwidth(fragUV) * 2.0;
                float uvThickness = thickness / cells;
                vec2 borderRenderThick = max(vec2(uvThickness), borderFw / 2.0);
                vec2 borderMask = smoothstep(1.0 - borderRenderThick - borderFw, 1.0 - borderRenderThick + borderFw, uvDist);
                float borderFactor = max(borderMask.x, borderMask.y);

                // --- 合并与输出 ---
                float finalFactor = max(gridFactor, borderFactor);

                if (finalFactor < 0.01) {
                    discard;
                }
                
                // 把算出来的抗锯齿边缘因子乘到 Alpha 上
                outColor = vec4(baseColor, 0.8 * finalFactor);
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
        float shininess = (matu.flags.x == 0u) ? 128.0 : 256.0; 
        float specFactor = pow(max(dot(N, H), 0.0), shininess); 

        float specIntensity = gl_FrontFacing ? 0.15 : 0.02;
        
        if(matu.flags.x == 1u && matu.flags.y == 1u) {
            specIntensity = 0.0; // 平面去除高光
        }
        specular += vec3(specIntensity) * lightColor * specFactor; 
    }

    // ==========================================
    // 4. 合成最终输出
    // ==========================================
    vec3 finalColor = ambient + diffuse + specular;
    
    // Reinhard 色调映射，柔和地压制强光，防止高光区发黄/死白
    finalColor = finalColor / (finalColor + vec3(1.0));

    // Gamma 校正
    finalColor = pow(finalColor, vec3(1.0 / 2.2));

    outColor = vec4(finalColor, matu.baseColorFactor.a);
}
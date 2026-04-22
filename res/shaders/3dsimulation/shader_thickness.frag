#version 450

// 从 shader.vert 接收的变量
layout(location = 1) in vec3 fragPosWorld;
layout(location = 2) in vec3 fragNormalWorld;

// 必须和主 Shader 保持完全相同的内存布局
struct PointLight {
    vec4 position;
    vec4 color;
};

layout(set = 0, binding = 0) uniform GlobalUbo {
    mat4 projection;
    mat4 view;
    mat4 invView;
    vec4 ambientLightColor;
    PointLight pointLights[20];
    int numLights;

    // 自定义视点剔除参数
    vec4 obsCamPos;
    int useCustomCulling;
} ubo;

layout(push_constant) uniform Push {
    mat4 modelMatrix;
    mat4 normalMatrix;
} push;

layout(location = 0) out vec4 outColor;

void main() {
    // 【逻辑 1】：剔除 X 轴坐标大于 0 的面片 (切开砂轮)
    if(fragPosWorld.x > 0.0) {
        discard;
    }

float objCenterX = push.modelMatrix[3].x;
    float distanceMoved = abs(objCenterX - (-0.642588));
    float trackFactor = clamp(distanceMoved / 15.0, 0.0, 1.0);

    float steps = 3.0;
    int stepIndex = int(round(trackFactor * steps));
    stepIndex = clamp(stepIndex, 0, 3);

    // 【新增】：把你主 Shader 里的硬编码数组完美复刻过来！
    vec3 frontColors[4] = vec3[](
        vec3(0.0078, 0.1333, 0.3882),
        vec3(0.0745, 0.2196, 0.6902),
        vec3(0.1255, 0.4549, 0.8314),
        vec3(0.1686, 0.7059, 0.8706)
    );
    vec3 backColors[4] = vec3[](
        vec3(0.1451, 0.1451, 0.1451),
        vec3(0.2353, 0.2353, 0.2353),
        vec3(0.3882, 0.3882, 0.3882),
        vec3(0.6784, 0.6784, 0.6784) 
    );

    // 【防过曝魔法】：因为我们在做纯加法叠加，所以要把原始颜色的光照能量降低。
    // 如果叠加后觉得太暗，就把这个值往上调 (例如 0.8)；如果中心太白看不清层次，就往下调 (例如 0.4)。
    float exposure = 0.8; 

    // 3. 根据正反面输出对应的颜色
    if (!gl_FrontFacing) {
        // 外壳：蓝色系 (注意加上和主 Shader 一样的 pow 2.2 转换)
        vec3 color = pow(frontColors[stepIndex], vec3(2.2)) * exposure;
        outColor = vec4(color, 1.0);
    } else {
        // 内壁：灰色系
        vec3 color = pow(backColors[stepIndex], vec3(2.2)) * exposure;
        outColor = vec4(color, 1.0);
    }

    // 【逻辑 2】：自定义视点正面剔除 (只画背面)
    // /*
    // if(ubo.useCustomCulling == 1) {
    //     vec3 dirToObsCam = normalize(ubo.obsCamPos.xyz - fragPosWorld);
    //     float dotResult = dot(normalize(fragNormalWorld), dirToObsCam);
        
    //     // 如果法线朝向观察点 (正面)，则剔除
    //     if (dotResult > 0.0) {
    //         discard; 
    //     }
    // }

    // // 累加厚度值 (如果你觉得太亮可以调小这个值，比如 0.10)
    // outColor = vec4(0.15, 0.15, 0.15, 1.0);
    // */

    // 正反面判定与颜色分离
    // 摄像机在 +X 看向 -X。正对我们的面就是正面(穿入)，背对我们的面就是背面(穿出)。
    // if (gl_FrontFacing) {
    //     // 射线穿过正面 (相当于模板值 +1)
    //     // 每次累加纯正的深蓝色
    //     outColor = vec4(0.0, 0.08, 0.25, 1.0); 
    // } else {
    //     // 射线穿过背面 (相当于模板值 -1)
    //     // 每次累加高级的暗灰色
    //     outColor = vec4(0.15, 0.15, 0.15, 1.0);
    // }
}
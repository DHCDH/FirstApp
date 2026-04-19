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

layout(location = 0) out vec4 outColor;

void main() {
    // 【逻辑 1】：剔除 X 轴坐标大于 0 的面片 (切开砂轮)
    if(fragPosWorld.x > 0.0) {
        discard;
    }

    // 【逻辑 2】：自定义视点正面剔除 (只画背面)
    /*
    if(ubo.useCustomCulling == 1) {
        vec3 dirToObsCam = normalize(ubo.obsCamPos.xyz - fragPosWorld);
        float dotResult = dot(normalize(fragNormalWorld), dirToObsCam);
        
        // 如果法线朝向观察点 (正面)，则剔除
        if (dotResult > 0.0) {
            discard; 
        }
    }

    // 累加厚度值 (如果你觉得太亮可以调小这个值，比如 0.10)
    outColor = vec4(0.15, 0.15, 0.15, 1.0);
    */

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
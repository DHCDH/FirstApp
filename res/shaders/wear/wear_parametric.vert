#version 450

// UV模板坐标
layout(location = 0) in vec2 inUV;

layout(location = 1) in vec4 modelRow0;
layout(location = 2) in vec4 modelRow1;
layout(location = 3) in vec4 modelRow2;
layout(location = 4) in vec4 modelRow3;

layout(location = 0) out vec3 vWorldPos;

layout(set = 0, binding = 0) uniform GlobalUbo {
    mat4 projection;
    mat4 view;
} ubo;

// SearchState SSBO: golden section search state managed on GPU
layout(set = 1, binding = 0, std140) buffer SearchState {
    float a;
    float b;
    float ratio;
    float tol;
    float x1;
    float x2;
    uint iteration;
    uint maxIters;
    uint converged;
    uint flags;
    float bestR;
    float _pad;
} searchState;

layout(push_constant, std140) uniform Push {
    layout(offset = 0)  vec3 normal;
    layout(offset = 16) vec4 grVec;
    layout(offset = 32) vec3 point;
    layout(offset = 48) vec4 gaVec;
    layout(offset = 64) float gR;
    layout(offset = 80) float width;
    layout(offset = 96) uint candidateIndex;
} push;

float calculate1V1GrindingWheelRadius(float u0)
{
    // 1V1砂轮只有两个圆角，一个斜角
    float gr1;
    if ((searchState.flags & 1u) != 0u) {
        // GPU-driven golden section: read candidate radius from SSBO
        gr1 = (push.candidateIndex == 0u) ? searchState.x1 : searchState.x2;
    } else {
        // Batch evaluation: read from push constant
        gr1 = push.grVec.x;
    }
    float gr2 = push.grVec.y;
    float ga = push.gaVec.x;
    float gb = push.width;

    float gR;
    if ((searchState.flags & 1u) != 0u) {
        // Recompute wheel max radius with actual corner radius from SSBO
        // gR = R + gr * (1 - 1/tan(ga/2))
        gR = push.gR + gr1 * (1.0 - 1.0 / tan(ga / 2.0));
    } else {
        gR = push.gR;    // 此处砂轮半径是磨圆角后的砂轮半径
    }

    if(u0 < gr1 + gr1 * cos(ga)) {
        return gR - gr1 + sqrt(max(0.0, gr1 * gr1 - (gr1 - u0) * (gr1 - u0)));
    } else if(u0 < gb - (gr2 - gr2 * cos(ga))) {
        return gR - gr1 + gr1 * sin(ga) -
                           (u0 - (gr1 + gr1 * cos(ga))) * (1.0 / tan(ga));
    // } else if(u0 >= gb - (gr2 - gr2 * cos(ga)) && u0 <= gb) {
    } else {
        return gR - gr1 * (1 - 1.0 / tan(ga / 2)) - gb / tan(ga) -
                           gr2 * tan(ga / 2) +
                           sqrt(max(0.0, gr2 * gr2 - (gr2 - gb + u0) * (gr2 - gb + u0)));
    }
}

float calculate1A1GrindingWheelRadius(float u0)
{
        // 1V1砂轮只有两个圆角，一个斜角
    float gr1;
    if ((searchState.flags & 1u) != 0u) {
        // GPU-driven golden section: read candidate radius from SSBO
        gr1 = (push.candidateIndex == 0u) ? searchState.x1 : searchState.x2;
    } else {
        // Batch evaluation: read from push constant
        gr1 = push.grVec.x;
    }
    float gr2 = push.grVec.y;
    float ga = push.gaVec.x;
    float gb = push.width;

    float gR = push.gR;

    if(u0 < gr1) {
        return gR - gr1 + sqrt(max(0.0, gr1 * gr1 - (gr1 - u0) * (gr1 - u0)));
    } else if(u0 < gb - gr2) {
        return gR;
    // } else if(u0 >= gb - (gr2 - gr2 * cos(ga)) && u0 <= gb) {
    } else {
        return gR - gr2 + sqrt(max(0.0, gr2 * gr2 - (u0 - (gb - gr2)) * (u0 - (gb - gr2))));
    }
}

void main()
{
    float uRaw = inUV.x;
    float theta = inUV.y * 2.0 * 3.1415926535;

    float epsilon = 0.01;

    float u0 = 0.0;
    float r = 0.0;

    if(uRaw < epsilon) {
        // --- 端圆面 ---
        float t = uRaw / epsilon;
        u0 = 0.0;
        r = mix(0.0, calculate1V1GrindingWheelRadius(0.0), t);
    } else if (uRaw > 1.0 - epsilon) {
        // --- 端圆面 ---
        float t = (1.0 - uRaw) / epsilon;
        u0 = push.width;
        r = mix(0.0, calculate1V1GrindingWheelRadius(push.width), t);
    } else {
        // --- 侧面 ---
        float t_linear = (uRaw - epsilon) / (1.0 - 2.0 * epsilon);
        // 使用非线性映射，将顶点向两端（圆角）挤压
        float t = smoothstep(0.0, 1.0, t_linear);
        t = smoothstep(0.0, 1.0, t);

        u0 = t * push.width;
        r = calculate1V1GrindingWheelRadius(u0);
    }

    // ================== 对齐 .obj 坐标系==================
    vec3 localPos = vec3(u0, r * cos(theta), r * sin(theta));

    // 砂轮反演需要将刀轨的y、z，ny、nz取反，原因未知
    vec4 modelRow0Invert = vec4(modelRow0.x, -modelRow0.y, -modelRow0.z, modelRow0.w);
    vec4 modelRow1Invert = vec4(-modelRow1.x, modelRow1.y, modelRow1.z, modelRow1.w);
    vec4 modelRow2Invert = vec4(-modelRow2.x, modelRow2.y, modelRow2.z, modelRow2.w);
    vec4 modelRow3Invert = vec4(modelRow3.x, -modelRow3.y, -modelRow3.z, modelRow3.w);

    mat4 instanceModel = mat4(modelRow0Invert, modelRow1Invert, modelRow2Invert, modelRow3Invert);
    // mat4 instanceModel = mat4(1.0);

    vec4 worldPos = instanceModel * vec4(localPos, 1.0);

    vWorldPos = worldPos.xyz;
    gl_Position = ubo.projection * ubo.view * worldPos;
}

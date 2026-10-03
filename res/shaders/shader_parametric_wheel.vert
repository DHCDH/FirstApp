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

layout(push_constant, std140) uniform Push {
    layout(offset = 0)  vec3 normal;
    layout(offset = 16) vec4 grVec;
    layout(offset = 32) vec3 point;
    layout(offset = 48) vec4 gaVec;
    layout(offset = 64) float gR;
    layout(offset = 80) float width;
} push;

float calculate1V1GrindingWheelRadius(float u0)
{
    float gr1 = push.grVec.x;
    float gr2 = push.grVec.y;
    float ga = push.gaVec.x;
    float gb = push.width;
    float gR = push.gR;

    if(u0 < gr1 + gr1 * cos(ga)) {
        return gR - gr1 + sqrt(max(0.0, gr1 * gr1 - (gr1 - u0) * (gr1 - u0)));
    } else if(u0 < gb - (gr2 - gr2 * cos(ga))) {
        return gR - gr1 + gr1 * sin(ga) -
                           (u0 - (gr1 + gr1 * cos(ga))) * (1.0 / tan(ga));
    } else {
        return gR - gr1 * (1.0 - 1.0 / tan(ga / 2.0)) - gb / tan(ga) -
                           gr2 * tan(ga / 2.0) +
                           sqrt(max(0.0, gr2 * gr2 - (gr2 - gb + u0) * (gr2 - gb + u0)));
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
        float t = smoothstep(0.0, 1.0, t_linear);
        t = smoothstep(0.0, 1.0, t);

        u0 = t * push.width;
        r = calculate1V1GrindingWheelRadius(u0);
    }

    vec3 localPos = vec3(u0, r * cos(theta), r * sin(theta));

    mat4 instanceModel = mat4(modelRow0, modelRow1, modelRow2, modelRow3);

    vec4 worldPos = instanceModel * vec4(localPos, 1.0);

    vWorldPos = worldPos.xyz;
    gl_Position = ubo.projection * ubo.view * worldPos;
}

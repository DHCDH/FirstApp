#version 430

// 根据砂轮初始位姿，创建刀轨

// 允许在顶点着色器中直接指定渲染到哪一层 Framebuffer
#extension GL_ARB_shader_viewport_layer_array : require

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec3 vWorldPos;

layout(set = 0, binding = 0) uniform GlobalUbo {
    mat4 projection;
    mat4 view;
} ubo;

layout(std430, set = 0, binding = 1) readonly buffer BasePose {
    mat4 poses[];
} basePoses;

layout(push_constant) uniform Push {
    layout(offset = 64) vec3 normal;
    layout(offset = 80) vec3 point;
    layout(offset = 96) float stepZ;
    layout(offset = 100) float tanHelixAngle;
    layout(offset = 104) float radius;
    layout(offset = 108) int stepsPerPose; // 每刀轨多少实例 (如 400)
}push;

void main()
{
    // --- 根据全局实例ID，推导当前是螺旋的第几步 ---
    int poseIndex = int(gl_InstanceIndex) / push.stepsPerPose;
    int stepIndex = int(gl_InstanceIndex) % push.stepsPerPose;

    mat4 basePose = basePoses.poses[poseIndex];

    float z = float(stepIndex) * push.stepZ;
    float theta = (z * push.tanHelixAngle) / push.radius;

    // --- 构建螺旋变换矩阵 ---
    mat4 transZ = mat4(1.);
    transZ[3][2] = z;
    mat4 rotZ = mat4(
        cos(theta),  sin(theta), 0.0, 0.0,
        -sin(theta), cos(theta), 0.0, 0.0,
        0.0,         0.0,        1.0, 0.0,
        0.0,         0.0,        0.0, 1.0
    );

    mat4 finalInstanceModel = (transZ * rotZ) * push.basePose;

    vec4 worldPos = finalInstanceModel * vec4(inPosition, 1.0);
    vWorldPos = worldPos.xyz;

    gl_Position = ubo.projection * ubo.view * worldPos;

    gl_Layer = poseIndex;
}
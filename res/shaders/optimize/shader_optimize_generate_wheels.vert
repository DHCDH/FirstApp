#version 450

// 根据砂轮初始位姿，创建刀轨

// 允许在顶点着色器中直接指定渲染到哪一层 Framebuffer
#extension GL_ARB_shader_viewport_layer_array : require

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec3 vWorldPos;

struct CameraData { 
    mat4 projView;
    vec4 mapInfo;    
};
layout(std430, set = 0, binding = 0) readonly buffer CameraSSBO {
    CameraData cameras[];
}cameraData;

layout(std430, set = 1, binding = 1) readonly buffer BasePose {
    mat4 poses[];
} basePoses;

layout(push_constant) uniform Push {
    layout(offset = 0)  vec3 normal;
    layout(offset = 16) vec3 point;
    layout(offset = 32) float stepZ;
    layout(offset = 36) float tanHelixAngle;
    layout(offset = 40) float radius;
    layout(offset = 44) int stepsPerPose;
}push;

void main()
{
    // --- 根据全局实例ID，推导当前是螺旋的第几步 ---
    int poseIndex = int(gl_InstanceIndex) / push.stepsPerPose;
    int stepIndex = int(gl_InstanceIndex) % push.stepsPerPose;

    mat4 basePose = basePoses.poses[poseIndex];

    float stepDist = float(stepIndex) * push.stepZ;    // 其实是X轴
    float theta = (stepDist * push.tanHelixAngle) / push.radius;

    // --- 构建螺旋变换矩阵 ---
    mat4 transX = mat4(1.);
    transX[3][0] = stepDist;
    mat4 rotX = mat4(
        1.0, 0.0,         0.0,         0.0,
        0.0, cos(theta),  sin(theta),  0.0,
        0.0, -sin(theta), cos(theta),  0.0,
        0.0, 0.0,         0.0,         1.0
    );

    mat4 finalInstanceModel = (transX * rotX) * basePose;

    vec4 worldPos = finalInstanceModel * vec4(inPosition, 1.0);
    vWorldPos = worldPos.xyz;

    mat4 myCamera = cameraData.cameras[poseIndex].projView;
    gl_Position = myCamera * worldPos;

    gl_Layer = poseIndex;
}
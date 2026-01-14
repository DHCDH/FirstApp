#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

// inputMask 现在读取的是 Stencil 值 (uint 类型)
layout(binding = 0) uniform usampler2D inputMask;
layout(binding = 1) uniform usampler2D inputEdge;

layout(push_constant) uniform Push {
    int showMode;       // 0: Solid, 1: Wireframe
    vec2 texelSize;     // 1/width, 1/height
} push;

// 辅助函数：解析当前像素的掩码状态
void parseMask(uint mask, out bool hasBlank, out bool isWheelSection, out bool isIntersection) {
    // 1. 棒料存在：Bit 0 为 1
    hasBlank = (mask & 1u) != 0u;
    
    // 2. 砂轮截面存在：Bit 1 (前) 和 Bit 2 (后) 同时为 1
    // 隐式计算核心：只有既在前投影里，又在后投影里，才是实体截面
    bool hasFront = (mask & 2u) != 0u;
    bool hasBack  = (mask & 4u) != 0u;
    isWheelSection = hasFront && hasBack;

    // 3. 交集存在：既是棒料又是砂轮截面
    isIntersection = hasBlank && isWheelSection;
}

void main()
{
    // 采样中心点
    uint mask_center = texture(inputMask, inUV).r;
    
    // 解析中心点状态
    bool c_Blank, c_Wheel, c_Inter;
    parseMask(mask_center, c_Blank, c_Wheel, c_Inter);

    if(push.showMode == 0) {
        /* ========= 实心模式 (Solid) ========= */
        if (c_Inter) {
            outColor = vec4(0., 0., 1., 1.);       // 交集蓝
        } else if (c_Blank) {
            outColor = vec4(0., 1., 0., 1.);       // 棒料绿
        } else if (c_Wheel) {
            outColor = vec4(1., 0., 0., 1.);       // 砂轮红
        } else {
            outColor = vec4(0.12, 0.12, 0.12, 1.); // 背景灰
        }
    } else {
        /* ========= 线框模式 (Edge Detection) ========= */
        
        // 获取背景信息
        uint mask_center = texture(inputMask, inUV).r;
        bool c_Blank = (mask_center & 1u) != 0u;

        vec4 baseColor = vec4(0.);
        if(c_Blank) {
            baseColor = vec4(0., 0.3, 0., 1.);
        }

        // 获取前景轮廓
        uint edgeVal = texture(inputEdge, inUV).r;
        bool isExplicitEdge = (edgeVal > 0u);

        // 合并绘制
        if (isExplicitEdge) {
            outColor = vec4(1.0, 0.0, 0.0, 1.0); // 直接画红线
        } else {
            outColor = baseColor;
        }
    }
}
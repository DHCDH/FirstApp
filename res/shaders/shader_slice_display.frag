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
    // 棒料 ID = 1 (二进制 01)
    // 砂轮 ID = 2 (二进制 10)
    // 交集 ID = 3 (二进制 11)
    hasBlank = (mask & 1u) != 0u;
    isWheelSection = (mask &2u) != 0u;
    isIntersection = (mask == 3u);
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
            outColor = vec4(1.0, 0.6, 0.0, 1.0);       // 交集
        } else if (c_Blank) {
            outColor = vec4(0.10, 0.40, 0.70, 1.0);       // 棒料
        } else if (c_Wheel) {
            outColor = vec4(0.90, 0.20, 0.15, 1.0);       // 砂轮
        } else {
            outColor = vec4(0.05, 0.05, 0.08, 1.0); // 背景
        }
    }
    // } else {
    //     /* ========= 线框模式 (Edge Detection) ========= */
        
    //     // 获取背景信息
    //     uint mask_center = texture(inputMask, inUV).r;
    //     bool c_Blank = (mask_center & 0x80u) != 0u;

    //     vec4 baseColor = vec4(0.);
    //     if(c_Blank) {
    //         baseColor = vec4(0.25, 0.35, 0.45, 1.); // 绿色棒料
    //     }
    //     outColor = baseColor;
    // }
}
//机床正解、逆解
// 将轨迹连成b样条做插值
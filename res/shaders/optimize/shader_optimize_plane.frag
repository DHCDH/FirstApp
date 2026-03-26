#version 450

layout(location = 0) in vec4 vClipPos;

void main() 
{
    float planeDepth = vClipPos.z / vClipPos.w;

    // 强制当前像素的深度写入为数学平面的深度
    gl_FragDepth = planeDepth;
}
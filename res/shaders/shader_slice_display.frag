#version 450

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform usampler2D inputMask;

void main()
{
    /*采样*/
    uint id = texture(inputMask, inUV).r;

    if(id == 1) {
        /*ID: 1, BLANK, GREEN*/
        outColor = vec4(0., 1., 0., 1.);
    } else if (id == 2) {
        /*ID: 2, GRINDING WHEEL, RED*/
        outColor = vec4(1., 0., 0., 1.);
    } else if (id == 3) {
        /*ID: 3, INTERSECTION, BLUE*/
        outColor = vec4(0., 0., 1., 1.);
    } else {
        /*BACKGROUND, DARK GREY*/
        outColor = vec4(0.12, 0.12, 0.12, 1.);
    }
}
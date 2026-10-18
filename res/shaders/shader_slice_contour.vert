#version 450

layout(location = 0) in vec2 inWorldPos;

layout(push_constant) uniform Push {
    vec4 bounds;  // xMin, xMax, zMin, zMax
} push;

void main()
{
    float xMin = push.bounds.x;
    float xMax = push.bounds.y;
    float zMin = push.bounds.z;
    float zMax = push.bounds.w;

    float ndcX = 2.0 * (inWorldPos.x - xMin) / (xMax - xMin) - 1.0;
    float ndcY = 2.0 * (inWorldPos.y - zMax) / (zMin - zMax) - 1.0;

    gl_Position = vec4(ndcX, ndcY, 0.0, 1.0);
    gl_PointSize = 16.0;
}

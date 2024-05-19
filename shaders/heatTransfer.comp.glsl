#version 460

layout(push_constant) uniform TInfo {
    int width;
    int height;
    float minTemperature;
    float maxTemperature;
};

layout (set = 0, binding = 0) buffer TCurrent
{
  readonly float currentT[];
};

layout (set = 0, binding = 1) buffer TTarget
{
  writeonly float targetT[];
};

void main()
{
    int x = int(gl_GlobalInvocationID.x);
    int y = int(gl_GlobalInvocationID.y);

    int left = (x > 0) ? x - 1 : 0;
    int right = (x < width - 1) ? x + 1 : width - 1;

    int top = (y > 0) ? y - 1 : 0;
    int bottom = (y < height - 1) ? y + 1 : height - 1;

    int offset = x + y * width;
    int offsetLeft = left + y * width;
    int offsetRight = right + y * width;
    int offsetTop = x + top * width;
    int offsetBottom = x + bottom * width;
    // offset diagonals
    int offsetTopLeft = left + top * width;
    int offsetTopRight = right + top * width;
    int offsetBottomLeft = left + bottom * width;
    int offsetBottomRight = right + bottom * width;

    targetT[offset] = currentT[offset] + .025 * ((currentT[offsetTop] + currentT[offsetBottom] + currentT[offsetLeft] + currentT[offsetRight] + currentT[offsetTopLeft] + currentT[offsetTopRight] + currentT[offsetBottomLeft] + currentT[offsetBottomRight]) - (currentT[offset] * 8.0));
}

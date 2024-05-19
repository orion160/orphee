#version 460

layout(push_constant) uniform TInfo {
    uint width;
    uint height;
    float minTemperature;
    float maxTemperature;
};

layout (set = 0, binding = 0) buffer Temperatures
{
  float T[];
};

layout(set = 0, rgba8, binding = 1) uniform writeonly image2D image;

vec4 temperatureToColor(float temperature) {
    vec4 blue = vec4(0.0, 0.0, 1.0, 1.0);
    vec4 green = vec4(0.0, 1.0, 0.0, 1.0);
    vec4 red = vec4(1.0, 0.0, 0.0, 1.0);

    if (temperature < 0.01) {
        return vec4(0.0, 0.0, 0.0, 1.0);
    }
    else if (temperature < 0.5) {
        return mix(blue, green, temperature * 2.0);
    } else {
        return mix(green, red, (temperature - 0.5) * 2.0);
    }
}

void main()
{
    ivec2 pixelCoords = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(image);

    if(pixelCoords.x < size.x && pixelCoords.y < size.y)
    {
        float temperature = T[pixelCoords.y * size.x + pixelCoords.x];
        float normalizedTemperature = (temperature - minTemperature) / (maxTemperature - minTemperature);

        vec4 color = temperatureToColor(normalizedTemperature);
        imageStore(image, pixelCoords, color);
    }
}

#version 460 core
layout (location = 0) in vec4 color;
layout (location = 1) in vec4 normal;
layout (location = 2) in vec2 texCoord;
layout (location = 3) in vec3 fragPosWorld;
layout (location = 4) flat in uint vInstanceIndex;

layout (location = 0) out vec4 FragColor;
layout (location = 1) out uint SelectedInstance;

layout (set = 0, binding = 0) uniform sampler2D tex;

layout (push_constant) uniform Constants {
  uint modelStride;
  uint instanceBaseIndex;
  uint skinMatrixOffset;
  uint pickId;
};

layout(set = 0, binding = 6) uniform LightsUbo {
    vec4 ambientLightColor;
    vec4 lightPositions[200];  // w component is radius
    vec4 lightColors[200];     // w component is intensity
    uint numLights;
} u_Lights;

float toSRGB(float x) {
if (x <= 0.0031308)
        return 12.92 * x;
    else
        return 1.055 * pow(x, (1.0/2.4)) - 0.055;
}
vec3 sRGB(vec3 c) {
    return vec3(toSRGB(c.x), toSRGB(c.y), toSRGB(c.z));
}

void main() {

    uint instanceId = vInstanceIndex + instanceBaseIndex;
    bool selected = (pickId == instanceId + 1);

    float ambientStrength = u_Lights.ambientLightColor.w;
    vec3 ambient = ambientStrength * u_Lights.ambientLightColor.rgb;

    vec3 norm = normalize(vec3(normal));
    vec3 diffuseLight = vec3(0.0);

    for(uint i = 0; i < u_Lights.numLights && i < 100; i++) {

        vec3 lightPosition = u_Lights.lightPositions[i].rgb;
        float lightRadius = u_Lights.lightPositions[i].w;

        vec3 lightColor = u_Lights.lightColors[i].rgb;
        float lightIntensity = u_Lights.lightColors[i].w;

        // Calculate direction vector between light source and fragment position
        vec3 L = lightPosition - fragPosWorld;
        float dist2 = dot(L, L);
        float dist = sqrt(dist2);

        // Inverse-square attenuation scaled by radius
        // Larger radius = stronger light contribution at same distance
        float attenuation = (lightIntensity * lightRadius) / (1.0 + dist2);

        vec3 directionToLight = L / dist;

        // The diffuse impact of the light
        float cosAngIncidence = max(dot(norm, directionToLight), 0);

        vec3 diffuse = lightColor * attenuation * cosAngIncidence;
        diffuseLight += diffuse;

    }

    FragColor = vec4(ambient + diffuseLight, 1.0) * texture(tex, texCoord) * color;
    FragColor.rgb = sRGB(FragColor.rgb);

    if(selected) {
        vec3 highlight = vec3(0.25, 0.25, 0.25);
        FragColor.rgb += highlight;
    }

    /* fill the second color attachment with the ID of our model */
    SelectedInstance = instanceId + 1;
}

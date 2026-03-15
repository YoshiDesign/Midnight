#version 460 core
#extension GL_EXT_nonuniform_qualifier : enable

const uint MAX_BINDLESS_TEXTURES = 64;

layout (location = 0) in vec4 color;
layout (location = 1) in vec4 normal;
layout (location = 2) in vec2 texCoord;
layout (location = 3) in vec3 fragPosWorld;
layout (location = 4) flat in uint vInstanceIndex;

layout (location = 0) out vec4 FragColor;
layout (location = 1) out uint SelectedInstance;

layout (set = 0, binding = 0) uniform sampler2D tex[MAX_BINDLESS_TEXTURES];

struct Material {
    uint baseTex;
    uint data_1;
    uint data_2;
    uint ext_index;
}; 

layout (std430, set = 0, binding = 7) readonly restrict buffer Materials {
  Material materials[];
};

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

    uint n = min(u_Lights.numLights, 5u);

    vec3 norm = normalize(normal.xyz);
    vec3 diffuseLight = vec3(0.0);

    for (uint i = 0u; i < 5u; ++i) {
        if (i >= n) break;

        vec3 lp = u_Lights.lightPositions[i].xyz;
        float radius = u_Lights.lightPositions[i].w;

        vec3 lc = u_Lights.lightColors[i].xyz;
        float intensity = u_Lights.lightColors[i].w;

        vec3 L = lp - fragPosWorld;
        float dist2 = dot(L, L) + 1e-8;

        float invDist = inversesqrt(dist2);
        vec3 dir = L * invDist;

        float NdotL = max(dot(norm, dir), 0.0);

        float attenuation = (intensity * radius) / (1.0 + dist2);

        diffuseLight += lc * (attenuation * NdotL);
    }

    // User vertex colors only
    if (materials[instanceId].data_1 == 1) {
        FragColor = vec4(ambient + diffuseLight, 1.0) * color;
    } 
    else  { 
        // Use Texture
        FragColor = vec4(ambient + diffuseLight, 1.0) * texture(tex[nonuniformEXT(materials[instanceId].baseTex)], texCoord) * color;
    }

    /* Note: Performing the sRGB conversion at the last minute is (apparently) best for preserving quality */
    FragColor.rgb = sRGB(FragColor.rgb); // Enable for gamma correction

    if(selected) {
        vec3 highlight = vec3(0.25, 0.25, 0.25);
        FragColor.rgb += highlight;
    }

    // FragColor = vec4(1, 0, 1, 1); // dbugger

    /* fill the second color attachment with the ID of our model */
    SelectedInstance = instanceId + 1;

}
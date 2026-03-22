#version 460

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vWorldNormal;
layout(location = 2) in vec3 vMatWeights;
layout(location = 3) in float vSteep;

layout(location = 0) out vec4 outColor;

// Set 0: Per-frame global data
layout(set = 0, binding = 2) uniform LightsUbo {
    vec4 ambientLightColor;    // w is intensity
    vec4 lightPositions[200];  // w is radius
    vec4 lightColors[200];     // w is intensity
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

    float ambientStrength = u_Lights.ambientLightColor.w;
    vec3 ambient = ambientStrength * u_Lights.ambientLightColor.rgb;

    uint n = min(u_Lights.numLights, 5u);

    vec3 norm = normalize(vWorldNormal);
    vec3 diffuseLight = vec3(0.0);

    for (uint i = 0u; i < 5u; ++i) {
        if (i >= n) break;

        vec3 lp = u_Lights.lightPositions[i].xyz;
        float radius = u_Lights.lightPositions[i].w;

        vec3 lc = u_Lights.lightColors[i].xyz;
        float intensity = u_Lights.lightColors[i].w;

        vec3 L = lp - vWorldPos;
        float dist2 = dot(L, L) + 1e-8;

        float invDist = inversesqrt(dist2);
        vec3 dir = L * invDist;

        float NdotL = max(dot(norm, dir), 0.0);

        float attenuation = (intensity * radius) / (1.0 + dist2);

        diffuseLight += lc * (attenuation * NdotL);
    }

    // Weight-tinted base color for visual verification
    vec3 mw = max(vMatWeights, vec3(0.0));
    float s = mw.x + mw.y + mw.z;
    mw = (s > 1e-8) ? (mw / s) : vec3(1.0, 0.0, 0.0);

    const vec3 grassColor = vec3(0.3, 0.55, 0.2);
    const vec3 rockColor  = vec3(0.45, 0.42, 0.38);
    const vec3 snowColor  = vec3(0.9, 0.92, 0.95);

    vec3 baseColor = grassColor * mw.x + rockColor * mw.y + snowColor * mw.z;

    outColor = vec4((ambient + diffuseLight) * baseColor, 1.0);
    outColor.rgb = sRGB(outColor.rgb);
}

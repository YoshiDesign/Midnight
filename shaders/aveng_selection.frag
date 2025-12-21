#version 460 core
layout (location = 0) in vec4 color;
layout (location = 1) in vec4 normal;
layout (location = 2) in vec2 texCoord;
layout (location = 3) flat in float selectInfo;
layout (location = 4) in vec3 fragPosWorld;

layout (location = 0) out vec4 FragColor;
layout (location = 1) out float SelectedInstance;

layout (set = 0, binding = 0) uniform sampler2D tex;

layout(set = 1, binding = 3) uniform LightsUbo {
    vec4 ambientLightColor;
    vec4 lightPositions[100];  // w component is radius
    vec4 lightColors[100];     // w component is intensity
    uint numLights;
} u_Lights;


vec3 lightPos = vec3(4.0, 3.0, 6.0);
vec3 lightColor = vec3(1.0, 1.0, 1.0);

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

    vec3 norm = normalize(vec3(normal));
    vec3 diffuseLight = vec3(0.0);

    for(uint i = 0; i < u_Lights.numLights && i < 100; i++) {

        vec3 lightPosition = u_Lights.lightPositions[i].rgb;
        float lightRadius = u_Lights.lightPositions[i].w;

        vec3 lightColor = u_Lights.lightColors[i].rgb;
        float lightIntensity = u_Lights.lightColors[i].w;

        // The first thing we need to calculate is the direction 
        // vector between the light source and the fragment's position.

        vec3 L = lightPosition - fragPosWorld;
        float dist2 = dot(L,L); // Scalar
        float r2 = lightRadius * lightRadius;

        if (dist2 > r2)
            continue;

        float normalizedDist2 = dist2 / r2; // 0 -> 1
        float attenuation = lightIntensity * (1.0 - normalizedDist2);

        vec3 directionToLight = L * inversesqrt(dist2);

        // The diffuse impact of the light
        float cosAngIncidence = max(dot(norm, directionToLight), 0);

        vec3 diffuse = lightColor * attenuation * cosAngIncidence;
        diffuseLight += diffuse;

    }

    FragColor = vec4(ambient + diffuseLight, 1.0) * texture(tex, texCoord) * color;
    FragColor.rgb = sRGB(FragColor.rgb);

    /* fill the second color attachment with the ID of our model */
    SelectedInstance = selectInfo;
}

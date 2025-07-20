#version 450

layout(set = 0, binding = 1) uniform sampler2D texSampler[8];
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragPosWorld;
layout(location = 2) in vec3 fragNormalWorld;
layout(location = 3) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform GlobalUbo {
	mat4 projection;
	mat4 view;
	vec4 ambientLightColor; // w is intensity
	vec3 lightPosition;
	vec4 lightColor;
} ubo;

layout(set = 1, binding = 0) uniform ObjectUniformData {
    uint texIndex;
} u_ObjData;

// Add access to the point lights data
struct PointLight {
	vec4 position; // w is radius
	vec4 color;    // w is intensity
};

layout(set = 2, binding = 0) uniform LightsUbo {
	uint numLights;
	PointLight lights[100];
} lightsUbo;

void main() {

    vec4 result = vec4(fragColor, 1.0);

    if (u_ObjData.texIndex != 8) {  // 8 will omit texture and default to vertex colors
        result = texture(texSampler[u_ObjData.texIndex], fragTexCoord);
    }

    // Gamma correction
    float gamma = 1.1;
    result.rgb = pow(result.rgb, vec3(1.0 / gamma));

    // Calculate ambient lighting
    vec3 ambientLight = ubo.ambientLightColor.xyz * ubo.ambientLightColor.w;
    
    // Initialize total lighting with ambient
    vec3 totalLighting = ambientLight;
    
    // Loop through all point lights and accumulate their contributions
        for (uint i = 0u; i < lightsUbo.numLights && i < 100u; i++) {
            PointLight light = lightsUbo.lights[i];
            
            // Calculate direction and distance to light
            vec3 lightDirection = light.position.xyz - fragPosWorld;
            float distance = length(lightDirection);
            
            // Avoid division by zero
            if (distance > 0.001) {
                lightDirection = lightDirection / distance; // normalize
                
                // Much more forgiving attenuation - lights can reach much further
                float lightRadius = light.position.w;
                float maxDistance = lightRadius * 50.0; // Lights affect up to 50x their radius
                float attenuation = 1.0 / (1.0 + 0.01 * distance + 0.001 * distance * distance);
                
                // Gradual falloff based on max distance
                if (distance > maxDistance) {
                    float falloff = 1.0 - min(1.0, (distance - maxDistance) / (maxDistance * 0.5));
                    attenuation *= max(0.0, falloff);
                }
                
                // Calculate diffuse lighting
                float NdotL = max(dot(normalize(fragNormalWorld), lightDirection), 0.0);
                vec3 diffuse = light.color.xyz * light.color.w * NdotL * attenuation;
                
                totalLighting += diffuse;
            }
        }

    outColor = vec4(totalLighting * result.rgb, 1.0);
}
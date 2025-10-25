#version 450

// Specialization constant for dynamic texture array size
layout(constant_id = 0) const int MAX_TEXTURES = 8;  // Default fallback value

layout(set = 0, binding = 1) uniform sampler2D texSampler[MAX_TEXTURES];
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragPosWorld;
layout(location = 2) in vec3 fragNormalWorld;
layout(location = 3) in vec2 fragTexCoord;
layout(location = 4) flat in int fragTextureIndex;  // Texture index from vertex shader

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform LightsUbo {
    uint numLights;
    vec4 lightPositions[100];  // w component is radius
    vec4 lightColors[100];     // w component is intensity
} u_Lights;

layout(set = 0, binding = 0) uniform GlobalUbo {
    mat4 projection;
    mat4 view;
    vec4 ambientLightColor;  // w is intensity
    vec3 lightPosition;      // Position of a single light
    vec4 lightColor;         // w is light intensity
    int renderMode;          // 0 = STANDARD, 1 = WIREFRAME, 2 = DISTORTED
    float time;              // For animated effects
} ubo;

void main() {
    vec3 diffuseLight = ubo.ambientLightColor.xyz * ubo.ambientLightColor.w;
    vec3 surfaceNormal = normalize(fragNormalWorld);
    
    // Process all lights in the scene
    for(uint i = 0; i < u_Lights.numLights && i < 100; i++) {
        vec3 lightPosition = u_Lights.lightPositions[i].xyz;
        float lightRadius = u_Lights.lightPositions[i].w;
        vec3 lightColor = u_Lights.lightColors[i].xyz;
        float lightIntensity = u_Lights.lightColors[i].w;
        
        vec3 directionToLight = lightPosition - fragPosWorld;
        float distanceToLight = length(directionToLight);
        
        // Point light attenuation - use radius to control effective range
        float effectiveRadius = lightRadius * 50.0; // Scale radius for effective range
        
        // Skip light if beyond effective range (early exit for performance)
        if (distanceToLight > effectiveRadius) {
            continue;
        }
        
        // Realistic point light attenuation with smooth falloff
        float normalizedDistance = distanceToLight / effectiveRadius;
        float attenuation = lightIntensity * max(0.0, (1.0 - normalizedDistance * normalizedDistance));
        
        // Apply lighting contribution
        directionToLight = normalize(directionToLight);
        float cosAngIncidence = max(dot(surfaceNormal, directionToLight), 0);
        vec3 lightContribution = lightColor * attenuation * cosAngIncidence;
        diffuseLight += lightContribution;
    }
    
    // Use texture index from vertex shader (per-instance)
    int texIndex = clamp(fragTextureIndex, 0, MAX_TEXTURES - 1);
    vec4 result = texture(texSampler[texIndex], fragTexCoord);
    
    // Apply lighting with gamma correction
    vec3 finalColor = result.xyz * diffuseLight;
    finalColor = pow(finalColor, vec3(1.0/2.2)); // Gamma correction
    
    // Apply toxic cloud effect if in DISTORTED mode (renderMode == 2)
    if (ubo.renderMode == 2) {
        // Create distortion based on screen position and time
        vec2 screenPos = fragTexCoord;
        float wave = sin(screenPos.y * 10.0 + ubo.time * 2.0) * 0.02;
        float noiseX = sin(screenPos.x * 15.0 + ubo.time * 1.5) * 0.01;
        
        // Apply toxic green color tint
        vec3 toxicColor = vec3(0.2, 1.0, 0.3); // Toxic green
        float desaturation = 0.4;
        
        // Desaturate the original color
        float gray = dot(finalColor, vec3(0.299, 0.587, 0.114));
        vec3 desaturated = mix(finalColor, vec3(gray), desaturation);
        
        // Apply toxic tint
        finalColor = mix(desaturated, desaturated * toxicColor, 0.6);
        
        // Add pulsing effect
        float pulse = (sin(ubo.time * 3.0) + 1.0) * 0.5;
        finalColor += toxicColor * pulse * 0.15;
        
        // Add some chromatic aberration simulation
        float aberration = wave + noiseX;
        finalColor.r *= (1.0 + aberration);
        finalColor.g *= (1.0 - aberration * 0.5);
        finalColor.b *= (1.0 + aberration * 0.3);
    }
    
    outColor = vec4(finalColor, result.a);
}

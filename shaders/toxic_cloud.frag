#version 450

// Post-processing fragment shader for toxic cloud effect
layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

// Input texture from the rendered scene
layout(set = 0, binding = 0) uniform sampler2D sceneTexture;

// Effect parameters
layout(set = 0, binding = 1) uniform EffectUbo {
    float time;
    float intensity;
    vec2 screenSize;
    float waveAmplitude;
    float colorShift;
} effectParams;

// Simple noise function for distortion
float random(vec2 st) {
    return fract(sin(dot(st.xy, vec2(12.9898, 78.233))) * 43758.5453123);
}

float noise(vec2 st) {
    vec2 i = floor(st);
    vec2 f = fract(st);
    float a = random(i);
    float b = random(i + vec2(1.0, 0.0));
    float c = random(i + vec2(0.0, 1.0));
    float d = random(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

void main() {
    vec2 uv = fragTexCoord;
    
    // Create wavy distortion effect
    float wave = sin(uv.y * 10.0 + effectParams.time * 2.0) * effectParams.waveAmplitude;
    float noiseVal = noise(uv * 8.0 + effectParams.time * 0.5);
    
    // Apply distortion to UV coordinates
    vec2 distortedUV = uv + vec2(wave + noiseVal * 0.02, sin(uv.x * 8.0 + effectParams.time) * 0.01);
    
    // Sample the scene texture with distorted coordinates
    vec4 sceneColor = texture(sceneTexture, distortedUV);
    
    // Apply toxic green color tint and desaturation
    vec3 toxicColor = vec3(0.2, 1.0, 0.3); // Toxic green
    float desaturation = 0.3;
    
    // Desaturate the original color
    float gray = dot(sceneColor.rgb, vec3(0.299, 0.587, 0.114));
    vec3 desaturated = mix(sceneColor.rgb, vec3(gray), desaturation);
    
    // Apply toxic tint
    vec3 finalColor = mix(desaturated, desaturated * toxicColor, effectParams.colorShift);
    
    // Add some pulsing effect
    float pulse = (sin(effectParams.time * 3.0) + 1.0) * 0.5;
    finalColor += toxicColor * pulse * 0.1;
    
    // Add chromatic aberration for extra "trippiness"
    vec2 aberrationOffset = vec2(0.002 * effectParams.intensity, 0.0);
    float r = texture(sceneTexture, distortedUV + aberrationOffset).r;
    float g = texture(sceneTexture, distortedUV).g;
    float b = texture(sceneTexture, distortedUV - aberrationOffset).b;
    
    finalColor = vec3(r, g, b) * (1.0 + toxicColor * effectParams.intensity * 0.3);
    
    outColor = vec4(finalColor, sceneColor.a);
} 
#version 460

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vWorldNormal;
layout(location = 2) in vec3 vMatWeights;   // grass/rock/snow (interpolated)

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2DArray uAlbedoArray;

layout(push_constant) uniform PC {
    float texScale;   // world->UV scale, e.g. 0.1
    float sharpness;  // e.g. 4.0
} pc;

// Which layer in the array corresponds to which material.
const int LAYER_GRASS = 0;
const int LAYER_ROCK  = 1;
const int LAYER_SNOW  = 2;

// Triplanar axis blend weights derived from the world normal.
// The 1e-8 prevents division by zero in degenerate cases (e.g. a zero normal,
// or extreme pow(sharpness) pushing components toward 0).
vec3 triplanarAxisWeights(vec3 Nws, float sharpness)
{
    vec3 n = normalize(Nws);
    vec3 w = abs(n);
    w = pow(w, vec3(sharpness));
    float denom = (w.x + w.y + w.z);
    return w / (denom + 1e-8);
}

vec3 triplanarSampleAlbedoLayer(vec3 Pws, vec3 axisW, float scale, int layer)
{
    // Axis projection UVs from world position
    // X weight uses YZ plane, Y weight uses XZ plane, Z weight uses XY plane
    vec2 uvX = Pws.yz * scale;
    vec2 uvY = Pws.xz * scale;
    vec2 uvZ = Pws.xy * scale;

    vec3 x = texture(uAlbedoArray, vec3(uvX, float(layer))).rgb;
    vec3 y = texture(uAlbedoArray, vec3(uvY, float(layer))).rgb;
    vec3 z = texture(uAlbedoArray, vec3(uvZ, float(layer))).rgb;

    return x * axisW.x + y * axisW.y + z * axisW.z;
}

void main()
{
    // Normalize interpolated normal again (interpolation does not preserve unit length)
    vec3 N = normalize(vWorldNormal);

    // Axis weights for triplanar blending
    vec3 axisW = triplanarAxisWeights(N, pc.sharpness);

    // Material weights coming from compute -> vertex -> fragment
    // They should sum to ~1 already, but small drift can happen; renormalize for safety.
    vec3 mw = max(vMatWeights, vec3(0.0));
    float s = mw.x + mw.y + mw.z;
    mw = (s > 1e-8) ? (mw / s) : vec3(1.0, 0.0, 0.0);

    // Sample each terrain layer from the array
    vec3 grass = triplanarSampleAlbedoLayer(vWorldPos, axisW, pc.texScale, LAYER_GRASS);
    vec3 rock  = triplanarSampleAlbedoLayer(vWorldPos, axisW, pc.texScale, LAYER_ROCK);
    vec3 snow  = triplanarSampleAlbedoLayer(vWorldPos, axisW, pc.texScale, LAYER_SNOW);

    // Blend layers by weights
    vec3 albedo = grass * mw.x + rock * mw.y + snow * mw.z;

    outColor = vec4(albedo, 1.0);
}

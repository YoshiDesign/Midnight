#version 460 core
layout (location = 0) in vec4 color;
layout (location = 1) in vec4 normal;
layout (location = 2) in vec2 texCoord;
layout (location = 3) flat in float selectInfo;

layout (location = 0) out vec4 FragColor;
layout (location = 1) out float SelectedInstance;

layout (set = 0, binding = 0) uniform sampler2D tex;

layout(set = 1, binding = 4) uniform LightsUbo {
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
  vec3 lightDir = normalize(vec3(lightPos));

  float diff = max(dot(norm, lightDir), 0.0);
  vec3 diffuse = diff * u_Lights.ambientLightColor.rgb;

  FragColor = vec4(ambient + diffuse, 1.0) * texture(tex, texCoord) * color;
  FragColor.rgb = sRGB(FragColor.rgb);

  /* fill the second color attachment with the ID of our model */
  SelectedInstance = selectInfo;
}

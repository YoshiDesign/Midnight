#version 450

// Full-screen quad vertices (no vertex buffer needed)
const vec2 quadVertices[6] = vec2[](
    vec2(-1.0, -1.0),  // Bottom left
    vec2( 1.0, -1.0),  // Bottom right  
    vec2(-1.0,  1.0),  // Top left
    
    vec2( 1.0, -1.0),  // Bottom right
    vec2( 1.0,  1.0),  // Top right
    vec2(-1.0,  1.0)   // Top left
);

const vec2 quadUVs[6] = vec2[](
    vec2(0.0, 0.0),  // Bottom left
    vec2(1.0, 0.0),  // Bottom right
    vec2(0.0, 1.0),  // Top left
    
    vec2(1.0, 0.0),  // Bottom right
    vec2(1.0, 1.0),  // Top right
    vec2(0.0, 1.0)   // Top left
);

layout(location = 0) out vec2 fragTexCoord;

void main() {
    // Generate full-screen quad without vertex buffer
    gl_Position = vec4(quadVertices[gl_VertexIndex], 0.0, 1.0);
    fragTexCoord = quadUVs[gl_VertexIndex];
} 
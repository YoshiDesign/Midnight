#pragma once
#include <vector>
#include "Utils/glm_includes.h"


namespace aveng {
    class EngineDevice;
    struct VkMesh;
    struct VkLineMesh;
    struct VkVertexBufferData;

    class VertexBuffer {
    public:
        static bool init(EngineDevice& engineDevice, VkVertexBufferData& vertexBufferData, unsigned int bufferSize);

        // Mesh Vertex Data
        static bool uploadData(EngineDevice& engineDevice, VkVertexBufferData& vertexBufferData, const VkMesh& vertexData);

        // Gizmo / Lines Vertex Data
        static bool uploadData(EngineDevice& engineDevice, VkVertexBufferData& vertexBufferData, const VkLineMesh& vertexData);

        // Unused as far as I can tell
        static bool uploadData(EngineDevice& engineDevice, VkVertexBufferData& vertexBufferData, const std::vector<glm::vec3>& vetrexData); // nice
        static void cleanup(EngineDevice& engineDevice, VkVertexBufferData& vertexBufferData);
    private:
        static bool uploadToGPU(EngineDevice& engineDevice, VkVertexBufferData& vertexBufferData);

    };

}
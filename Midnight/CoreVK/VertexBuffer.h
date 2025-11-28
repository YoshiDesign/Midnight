#pragma once
#include <vector>
#include <vulkan/vulkan.h>
#include "Utils/glm_includes.h"

class EngineDevice;

#include "VkRenderData.h"
namespace aveng {

    class VertexBuffer {
    public:
        static bool init(EngineDevice& engineDevice, VkVertexBufferData& vertexBufferData, unsigned int bufferSize);

        // Mesh Vertex Data
        static bool uploadData(EngineDevice& engineDevice, VkVertexBufferData& vertexBufferData, VkMesh vertexData);

        // Gizmo / Lines Vertex Data
        static bool uploadData(EngineDevice& engineDevice, VkVertexBufferData& vertexBufferData, VkLineMesh vertexData);

        // Unused as far as I can tell
        static bool uploadData(EngineDevice& engineDevice, VkVertexBufferData& vertexBufferData, std::vector<glm::vec3> vetrexData);
        static void cleanup(EngineDevice& engineDevice, VkVertexBufferData& vertexBufferData);
    private:
        static bool uploadToGPU(EngineDevice& engineDevice, VkVertexBufferData& vertexBufferData);

    };

}
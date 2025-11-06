#pragma once
#include <vector>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include "CoreVK/EngineDevice.h"

#include "VkRenderData.h"
namespace aveng {

    class VertexBuffer {
    public:
        static bool init(EngineDevice& engineDevice, VkVertexBufferData& vertexBufferData, unsigned int bufferSize);

        static bool uploadData(EngineDevice& engineDevice, VkVertexBufferData& vertexBufferData, VkMesh vertexData);
        static bool uploadData(EngineDevice& engineDevice, VkVertexBufferData& vertexBufferData, std::vector<glm::vec3> vetrexData);

        static void cleanup(EngineDevice& engineDevice, VkVertexBufferData& vertexBufferData);
    };

}
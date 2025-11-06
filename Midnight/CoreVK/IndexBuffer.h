#pragma once

#include <vulkan/vulkan.h>
#include "CoreVK/EngineDevice.h"
#include "VkRenderData.h"
namespace aveng {
    class IndexBuffer {
    public:
        static bool init(EngineDevice& engineDevice, VkIndexBufferData& bufferData, size_t bufferSize);
        static bool uploadData(EngineDevice& engineDevice, VkIndexBufferData& bufferData, VkMesh vertexData);
        static void cleanup(EngineDevice& engineDevice, VkIndexBufferData& bufferData);
    };

}
#pragma once
/* Vulkan shader storage buffer object */
#pragma once
#include <memory>
#include <vector>
#include <cstdint>
#include <vulkan/vulkan.h>
#include "Utils/glm_includes.h"

#include "CoreVK/VkRenderData.h"
#include "CoreVK/aveng_buffer.h"

namespace aveng {
    class ShaderStorageBuffer {
    public:
        static bool checkForResize(VkRenderData& renderData, std::unique_ptr<AvengBuffer>& ssbo, size_t bufferSize)
        {

            if (bufferSize > ssbo->getBufferSize()) {
                std::printf("%s: [0] resize SSBO %p from %zu to %zu bytes\n",
                    __FUNCTION__,
                    ssbo->getBuffer(),
                    ssbo->getBufferSize(),
                    bufferSize);
                return true;
            }
            return false;
        }

        template <typename T>
        static bool uploadSsboData(VkRenderData& renderData, std::unique_ptr<AvengBuffer>& ssbo, std::vector<T>& bufferData)
        {
            if (bufferData.empty()) {
                return false;
            }

            // you probably meant size in bytes, not just element count
            const size_t dataSize = bufferData.size() * sizeof(T);
            const size_t requiredSize = bufferData.size() * ssbo->getAlignmentSize();

            bool bufferResized = false;

            // compare bytes-to-bytes
            if (requiredSize > ssbo->getBufferSize()) {
                std::printf("%s: [1] resize SSBO %p from %zu to %zu bytes\n",
                    __FUNCTION__,
                    ssbo->getBuffer(),
                    ssbo->getBufferSize(),
                    dataSize);
                bufferResized = true;
            }

            VkResult result = ssbo->map();
            if (result != VK_SUCCESS) {
                std::printf("%s error: could not map SSBO memory (error: %i)\n",
                    __FUNCTION__, result);
                bufferResized = true;
            }

            // you probably want to pass the data to the buffer:
            ssbo->writeToBuffer(bufferData.data(), dataSize);

            ssbo->unmap();   // you commented this out, but typically you unmap
            ssbo->flush();   // if your AvengBuffer supports it

            return bufferResized;
        }
    };

}
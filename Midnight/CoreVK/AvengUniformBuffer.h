#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include "VkRenderData.h"
#include "CoreVK/aveng_buffer.h"

namespace aveng {
    class UniformBuffer {
    public:
        template <typename T>
        static void uploadData(std::unique_ptr<AvengBuffer>& ubo, T* data) {
           
            ubo->writeToBuffer(data, sizeof(T));
            ubo->flush();

        }

    };

}

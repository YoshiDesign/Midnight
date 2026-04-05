#pragma once
#include <vector>
#include "CoreVK/VkRenderData.h"
#include "CoreVK/VertexBuffer.h"
#include "CoreVK/IndexBuffer.h"
#include "CoreVK/AvengStorageBuffer.h"

namespace aveng {

    class EngineDevice;

    struct TerrainResourcePool {
        std::vector<VkVertexBufferData>          vbo;
        std::vector<VkIndexBufferData>           ibo;
        std::vector<VkShaderStorageBufferData>   inputSsbo;
        std::vector<VkShaderStorageBufferData>   outputSsbo;

        static constexpr size_t kMaxPooledBuffers = 16;

        void destroyAll(EngineDevice& engineDevice) {
            for (auto& v : vbo)         VertexBuffer::cleanup(engineDevice, v);
            for (auto& i : ibo)         IndexBuffer::cleanup(engineDevice, i);
            for (auto& s : inputSsbo)   ShaderStorageBuffer::destroy(engineDevice, s);
            for (auto& s : outputSsbo)  ShaderStorageBuffer::destroy(engineDevice, s);
            vbo.clear();
            ibo.clear();
            inputSsbo.clear();
            outputSsbo.clear();
        }
    };

}

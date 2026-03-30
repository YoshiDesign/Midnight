#pragma once
#include <vector>
#include <memory>
#include "CoreVK/VkRenderData.h"
#include "CoreVK/VertexBuffer.h"
#include "CoreVK/IndexBuffer.h"
#include "CoreVK/AvengStorageBuffer.h"

namespace procgen { struct TerrainRenderable; }

namespace aveng {

    class EngineDevice;

    struct TerrainResourcePool {
        std::vector<VkVertexBufferData>          vbo;
        std::vector<VkIndexBufferData>           ibo;
        std::vector<VkShaderStorageBufferData>   inputSsbo;
        std::vector<VkShaderStorageBufferData>   outputSsbo;
        std::vector<std::unique_ptr<procgen::TerrainRenderable>> renderables;

        static constexpr size_t kMaxPooledRenderables = 32;

        void destroyAll(EngineDevice& engineDevice) {
            for (auto& v : vbo)         VertexBuffer::cleanup(engineDevice, v);
            for (auto& i : ibo)         IndexBuffer::cleanup(engineDevice, i);
            for (auto& s : inputSsbo)   ShaderStorageBuffer::destroy(engineDevice, s);
            for (auto& s : outputSsbo)  ShaderStorageBuffer::destroy(engineDevice, s);
            vbo.clear();
            ibo.clear();
            inputSsbo.clear();
            outputSsbo.clear();
            renderables.clear();
        }
    };

}

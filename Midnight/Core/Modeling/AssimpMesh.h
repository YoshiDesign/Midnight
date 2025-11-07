#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <memory>
#include <assimp/scene.h>

#include "CoreVK/VkRenderData.h"
#include "Core/Texture.h"
#include "Core/Modeling/AssimpBone.h"

namespace aveng {
    class AssimpMesh {
    public:
        // TODO - You're drilling a ref to the engine Device into this class, consider moving the entire engine device to VkRenderData
        bool processMesh(
            VkRenderData& renderData, 
            EngineDevice& engineDevice,
            aiMesh* mesh, 
            const aiScene* scene,
            std::string assetDirectory,
            std::unordered_map<std::string, VkTextureData>& textures);

        std::string getMeshName();
        unsigned int getTriangleCount();
        unsigned int getVertexCount();

        VkMesh getMesh();
        std::vector<uint32_t> getIndices();
        std::vector<std::shared_ptr<AssimpBone>> getBoneList();

    private:
        std::string mMeshName;
        unsigned int mTriangleCount = 0;
        unsigned int mVertexCount = 0;

        glm::vec4 mBaseColor = glm::vec4(1.0f);

        VkMesh mMesh{};
        std::vector<std::shared_ptr<AssimpBone>> mBoneList{};
    };
}
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
        
        bool processMesh(
            VkRenderData& renderData, 
            EngineDevice& engineDevice,
            aiMesh* mesh, 
            const aiScene* scene,
           /* std::string assetDirectory,*/
            const std::string modelBaseDir, const std::string contentRoot,
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
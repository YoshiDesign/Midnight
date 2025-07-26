#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <memory>

#include <assimp/scene.h>
#include "AssimpBone.h"
#include "Core/data.h"

namespace aveng {

// Forward declarations
class Logger;

class AssimpMesh {
public:
    bool processMesh(RenderData &renderData, aiMesh* mesh, const aiScene* scene);

    std::string getMeshName();
    unsigned int getTriangleCount();
    unsigned int getVertexCount();

    VkMesh getMesh();
    std::vector<uint32_t> getIndices();
    std::vector<std::shared_ptr<AssimpBone>> getBoneList();

private:
    void processBoneWeights(aiMesh* mesh);

    std::string mMeshName;
    unsigned int mTriangleCount = 0;
    unsigned int mVertexCount = 0;

    VkMesh mMesh{};
    std::vector<std::shared_ptr<AssimpBone>> mBoneList{};
};

} // namespace aveng
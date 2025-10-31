/* Assimp model, ready to draw */
#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <memory>
#include <unordered_map>

#include <glm/glm.hpp>

#include "AssimpMesh.h"
#include "AssimpNode.h"
#include "AssimpAnimClip.h"

#include "../data.h"

namespace aveng {

class AssimpModel {
public:
    bool loadModel(RenderData &renderData, std::string modelFilename, unsigned int extraImportFlags = 0);
    glm::mat4 getRootTransformationMatrix();

    unsigned int getTriangleCount();

    std::string getModelFileName();
    std::string getModelFileNamePath();

    //bool hasAnimations();
    //const std::vector<std::shared_ptr<AssimpAnimClip>>& getAnimClips();

    //const std::vector<std::shared_ptr<AssimpNode>>& getNodeList();
    //const std::unordered_map<std::string, std::shared_ptr<AssimpNode>>& getNodeMap();

    //const std::vector<std::shared_ptr<AssimpBone>>& getBoneList();
    //const std::vector<VkMesh>& getModelMeshes();

    //VkShaderStorageBufferData& getBoneMatrixOffsetBuffer();
    //VkShaderStorageBufferData& getBoneParentBuffer();

    void cleanup(RenderData &renderData);

private:
    void processNode(RenderData &renderData, std::shared_ptr<AssimpNode> node, aiNode* aNode, const aiScene* scene, std::string assetDirectory);
    //bool createDescriptorSet(RenderData &renderData);

    unsigned int mTriangleCount = 0;
    unsigned int mVertexCount = 0;

    /* store the root node for direct access */
    std::shared_ptr<AssimpNode> mRootNode = nullptr;

    /* a map to find the node by name */
    std::unordered_map<std::string, std::shared_ptr<AssimpNode>> mNodeMap{};
    /* and a 'flat' map to keep the order of insertion  */
    std::vector<std::shared_ptr<AssimpNode>> mNodeList{};

    std::vector<std::shared_ptr<AssimpBone>> mBoneList;
    std::vector<std::shared_ptr<AssimpAnimClip>> mAnimClips{};

    std::vector<VkMesh> mModelMeshes{};
    std::vector<VkVertexBufferData> mVertexBuffers{};
    std::vector<VkIndexBufferData> mIndexBuffers{};

    VkShaderStorageBufferData mShaderBoneParentBuffer{};
    VkShaderStorageBufferData mShaderBoneMatrixOffsetBuffer{};

    // map textures to external or internal texture names
    std::unordered_map<std::string, VkTextureData> mTextures{};

    glm::mat4 mRootTransformMatrix = glm::mat4(1.0f);

    std::string mModelFilenamePath;
    std::string mModelFilename;

};

} // namespace aveng
#include <algorithm>
#include <filesystem>
#include <cmath>

#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>

#include "AssimpModel.h"
#include "Logger.h"
#include "Tools.h"

namespace aveng { 

bool AssimpModel::loadModel(RenderData &renderData, std::string modelFilename, unsigned int extraImportFlags) {
    Assimp::Importer importer;
    
    // Essential flags for proper mesh loading and deformation debugging
    const aiScene *scene = importer.ReadFile(modelFilename,
        aiProcess_CalcTangentSpace |
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_GenNormals |
        aiProcess_FlipUVs |                 //  ESSENTIAL: Flip V for Vulkan
        aiProcess_ValidateDataStructure |   // Validate mesh integrity
        extraImportFlags
        // aiProcess_SortByPType |
        // aiProcess_LimitBoneWeights |        // CRITICAL: Limit to 4 bones per vertex
        // aiProcess_GenSmoothNormals |        //  Generate normals if missing  
        // aiProcess_FixInfacingNormals |      //  Fix inverted normals
        // aiProcess_ImproveCacheLocality 
        //aiProcess_Triangulate | 
        //aiProcess_ValidateDataStructure | 
        //aiProcess_FlipUVs |                    // Flip UVs for Vulkan
        //aiProcess_LimitBoneWeights |           // Limit to 4 bones per vertex
        
        );

    if(!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        Logger::log(0, "AssimpModel: Error loading '%s': %s\n", modelFilename.c_str(), importer.GetErrorString());
        return false;
    }

    // Extract directory path for textures
    std::string assetDirectory = std::filesystem::path(modelFilename).parent_path().string();
    if (!assetDirectory.empty()) {
        assetDirectory += "/";
    }

    unsigned int numMeshes = scene->mNumMeshes;
    Logger::log(1, "AssimpModel: Found %d mesh%s in '%s'\n", numMeshes, numMeshes == 1 ? "" : "es", modelFilename.c_str());

    // Count vertices and faces
    for (unsigned int i = 0; i < numMeshes; ++i) {
        //unsigned int numVertices = 
        //unsigned int numFaces = 

        mVertexCount += scene->mMeshes[i]->mNumVertices;
        mTriangleCount += scene->mMeshes[i]->mNumFaces;
        // Logger::log(1, "%s: mesh %i contains %i vertices and %i faces\n", __FUNCTION__, i, numVertices, numFaces);
    }
    Logger::log(1, "AssimpModel: Total %d vertices and %d faces\n", mVertexCount, mTriangleCount);

    aiNode* rootNode = scene->mRootNode;

  //// This looks for embedded textures.
  //if (scene->HasTextures()) {
  //  unsigned int numTextures = scene->mNumTextures;

  //  for (int i = 0; i < scene->mNumTextures; ++i) {
  //    std::string texName = scene->mTextures[i]->mFilename.C_Str();

  //    int height = scene->mTextures[i]->mHeight;
  //    int width = scene->mTextures[i]->mWidth;
  //    aiTexel* data = scene->mTextures[i]->pcData;

  //    VkTextureData newTex{};
  //    if (!Texture::loadTexture(renderData, newTex, texName, data, width, height)) {
  //      return false;
  //    }

  //    std::string internalTexName = "*" + std::to_string(i);
  //    Logger::log(1, "%s: - added internal texture '%s'\n", __FUNCTION__, internalTexName.c_str());
  //    mTextures.insert({internalTexName, newTex});
  //  }

  //  Logger::log(1, "%s: scene has %i embedded textures\n", __FUNCTION__, numTextures);
  //} 
  //else {
  //    Logger::log(1, "%s: The scene has no embedded textures [0]\n", __FUNCTION__);
  //}

  /* nodes */
  Logger::log(1, "%s: ... processing nodes...\n", __FUNCTION__);

  std::string rootNodeName = rootNode->mName.C_Str();
  mRootNode = AssimpNode::createNode(rootNodeName);
  Logger::log(2, "%s: root node name: '%s'\n", __FUNCTION__, rootNodeName.c_str());

  processNode(renderData, mRootNode, rootNode, scene, assetDirectory);

  Logger::log(1, "%s: ... processing nodes finished...\n", __FUNCTION__);

  for (const auto& entry : mNodeList) {
    std::vector<std::shared_ptr<AssimpNode>> childNodes = entry->getChilds();

    std::string parentName = entry->getParentNodeName();
    Logger::log(1, "%s: --- found node %s in node list, it has %i children, parent is %s\n", __FUNCTION__, entry->getNodeName().c_str(), childNodes.size(), parentName.c_str());

    for (const auto& node : childNodes) {
      Logger::log(1, "%s: ---- child: %s\n", __FUNCTION__, node->getNodeName().c_str());
    }
  }

  std::vector<glm::mat4> boneOffsetMatricesList{};
  std::vector<int32_t> boneParentIndexList{};

  for (const auto& bone : mBoneList) {
    boneOffsetMatricesList.emplace_back(bone->getOffsetMatrix());

    std::string parentNodeName = mNodeMap.at(bone->getBoneName())->getParentNodeName();
    const auto boneIter = std::find_if(mBoneList.begin(), mBoneList.end(), [parentNodeName](std::shared_ptr<AssimpBone>& bone) { return bone->getBoneName() == parentNodeName; });
    if (boneIter == mBoneList.end()) {
        boneParentIndexList.emplace_back(-1); // root node gets a -1 to identify
    } else {
        boneParentIndexList.emplace_back(std::distance(mBoneList.begin(), boneIter));
    }
  }

  Logger::log(1, "%s: -- bone parents --\n", __FUNCTION__);
  for (unsigned int i = 0; i < mBoneList.size(); ++i) {
    Logger::log(1, "%s: bone %i (%s) has parent %i (%s)\n", __FUNCTION__, i, mBoneList.at(i)->getBoneName().c_str(), boneParentIndexList.at(i),
      boneParentIndexList.at(i) < 0 ? "invalid" : mBoneList.at(boneParentIndexList.at(i))->getBoneName().c_str());
  }
  Logger::log(1, "%s: -- bone parents --\n", __FUNCTION__);

  /* create vertex buffers for the meshes */
  for (const auto& mesh : mModelMeshes) {

  }

  /* init all SSBOs */
  // TODO: Implement this.

  /* create descriptor set for per-model data */
  createDescriptorSet(renderData);

  /* animations */
  unsigned int numAnims = scene->mNumAnimations;
  for (unsigned int i = 0; i < numAnims; ++i) {
    aiAnimation* animation = scene->mAnimations[i];

    Logger::log(1, "%s: -- animation clip %i has %i skeletal channels, %i mesh channels, and %i morph mesh channels\n",
      __FUNCTION__, i, animation->mNumChannels, animation->mNumMeshChannels, animation->mNumMorphMeshChannels);

    std::shared_ptr<AssimpAnimClip> animClip = std::make_shared<AssimpAnimClip>();
    animClip->addChannels(animation, mBoneList);
    if (animClip->getClipName().empty()) {
      animClip->setClipName(std::to_string(i));
    }
    mAnimClips.emplace_back(animClip);
  }
  mModelFilenamePath = modelFilename;
  mModelFilename = std::filesystem::path(modelFilename).filename().generic_string();
  /* get root transformation matrix from model's root node */
  mRootTransformMatrix = Tools::convertAiToGLM(rootNode->mTransformation);
  Logger::log(1, "%s: - model has a total of %i bone%s\n", __FUNCTION__, mBoneList.size(), mBoneList.size() == 1 ? "" : "s");
  Logger::log(1, "%s: - model has a total of %i animation%s\n", __FUNCTION__, numAnims, numAnims == 1 ? "" : "s");
  Logger::log(1, "%s: successfully loaded model '%s' (%s)\n", __FUNCTION__, modelFilename.c_str(), mModelFilename.c_str());
  return true;
}

void AssimpModel::processNode(RenderData &renderData, std::shared_ptr<AssimpNode> node, aiNode* aNode, const aiScene* scene, std::string assetDirectory) {
    std::string nodeName = aNode->mName.C_Str();
    Logger::log(2, "AssimpModel: Processing node '%s'\n", nodeName.c_str());
    
    // Set node transformation from Assimp
    glm::mat4 nodeTransform = Tools::convertAiToGLM(aNode->mTransformation);
    node->setRootTransformMatrix(nodeTransform);

    // Process meshes in this node
    unsigned int numMeshes = aNode->mNumMeshes;
    if (numMeshes > 0) {
        Logger::log(1, "AssimpModel: Node '%s' has %d meshes\n", nodeName.c_str(), numMeshes);
        for (unsigned int i = 0; i < numMeshes; ++i) {
            aiMesh* modelMesh = scene->mMeshes[aNode->mMeshes[i]];

            AssimpMesh mesh;
            if (mesh.processMesh(renderData, modelMesh, scene)) {

                mModelMeshes.emplace_back(mesh.getMesh());

                // Collect bones, avoiding duplicates
                std::vector<std::shared_ptr<AssimpBone>> meshBones = mesh.getBoneList();

                for (const auto& bone : meshBones) {
                    // Check if bone already exists by name (not ID, since IDs might conflict)
                    auto iter = std::find_if(mBoneList.begin(), mBoneList.end(), 
                        [bone](std::shared_ptr<AssimpBone>& existingBone) { 
                            return bone->getBoneId() == existingBone->getBoneId();
                        });
                    if (iter == mBoneList.end()) {
                        mBoneList.emplace_back(bone);
                    }
                }
            }
        }
    }

    // Add node to maps
    mNodeMap.insert({nodeName, node});
    mNodeList.emplace_back(node);

    // Process child nodes
    unsigned int numChildren = aNode->mNumChildren;
    if (numChildren > 0) {
        Logger::log(2, "AssimpModel: Node '%s' has %d children\n", nodeName.c_str(), numChildren);
    }

    for (unsigned int i = 0; i < numChildren; ++i) {
        std::string childName = aNode->mChildren[i]->mName.C_Str();
        std::shared_ptr<AssimpNode> childNode = node->addChild(childName);
        processNode(renderData, childNode, aNode->mChildren[i], scene, assetDirectory);
    }
}

glm::mat4 AssimpModel::getRootTransformationMatrix() {
    return mRootTransformMatrix;
}

unsigned int AssimpModel::getTriangleCount() {
  return mTriangleCount;
}

std::string AssimpModel::getModelFileName() {
  return mModelFilename;
}

std::string AssimpModel::getModelFileNamePath() {
  return mModelFilenamePath;
}

const std::vector<std::shared_ptr<AssimpNode>>& AssimpModel::getNodeList() {
  return mNodeList;
}

const std::unordered_map<std::string, std::shared_ptr<AssimpNode>>& AssimpModel::getNodeMap() {
  return mNodeMap;
}

const std::vector<std::shared_ptr<AssimpBone>>& AssimpModel::getBoneList() {
    return mBoneList;
}

const std::vector<VkMesh>& AssimpModel::getModelMeshes() {
    return mModelMeshes;
}

const std::vector<std::shared_ptr<AssimpAnimClip>>& AssimpModel::getAnimClips() {
    return mAnimClips;
}

bool AssimpModel::hasAnimations() {
    return !mAnimClips.empty();
}

VkShaderStorageBufferData& AssimpModel::getBoneMatrixOffsetBuffer() {
    return mShaderBoneMatrixOffsetBuffer;
}

VkShaderStorageBufferData& AssimpModel::getBoneParentBuffer() {
    return mShaderBoneParentBuffer;
}

bool AssimpModel::createDescriptorSet(RenderData &renderData) {
    // Placeholder for descriptor set creation
    return true;
}

void AssimpModel::cleanup(RenderData &renderData) {
    // Cleanup resources
    mModelMeshes.clear();
    mBoneList.clear();
    mAnimClips.clear();
    mNodeList.clear();
    mNodeMap.clear();
    mTextures.clear();
}

}
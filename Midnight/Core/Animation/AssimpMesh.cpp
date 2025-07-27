#include "AssimpMesh.h"
#include "Logger.h"
#include "Tools.h"

namespace aveng {

bool AssimpMesh::processMesh(RenderData &renderData, aiMesh* mesh, const aiScene* scene) {
    mMeshName = mesh->mName.C_Str();
    mVertexCount = mesh->mNumVertices;
    mTriangleCount = mesh->mNumFaces;
    
    // Initialize mesh data
    mMesh.vertices.reserve(mVertexCount);
    mMesh.indices.reserve(mTriangleCount * 3);
    mMesh.name = mMeshName;
    
    Logger::log(1, "AssimpMesh: Processing mesh '%s' with %d vertices and %d faces\n", 
                mMeshName.c_str(), mVertexCount, mTriangleCount);
    
    // Process vertices
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        AnimatedVertex vertex{};
        
        // Position
        if (mesh->mVertices) {
            vertex.position = Tools::convertAiToGLM(mesh->mVertices[i]);
        }
        
        // Normal
        if (mesh->mNormals) {
            vertex.normal = Tools::convertAiToGLM(mesh->mNormals[i]);
        }
        
        // Texture coordinates (use first UV channel)
        if (mesh->mTextureCoords[0]) {
            vertex.texCoord = glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y);
        }
        
        // Default color (can be overridden by materials later)
        vertex.color = glm::vec3(1.0f, 1.0f, 1.0f);
        
        mMesh.vertices.push_back(vertex);
    }
    
    // Process indices
    for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
        const aiFace& face = mesh->mFaces[i];
        if (face.mNumIndices == 3) {
            std::cout << "[DEBUG] Processing Indices" << std::endl;
            mMesh.indices.push_back(face.mIndices[0]);
            mMesh.indices.push_back(face.mIndices[1]);
            mMesh.indices.push_back(face.mIndices[2]);
        }
    }
    
    // Process bone weights
    if (mesh->mNumBones > 0) {
        mMesh.hasAnimationData = true;
        processBoneWeights(mesh);
        Logger::log(1, "AssimpMesh: Processed %d bones for mesh '%s'\n", mesh->mNumBones, mMeshName.c_str());
    }
    
    Logger::log(1, "AssimpMesh: Successfully processed mesh '%s'\n", mMeshName.c_str());
    return true;
}

void AssimpMesh::processBoneWeights(aiMesh* mesh) {
    // Initialize all bone data to defaults
    for (auto& vertex : mMesh.vertices) {
        vertex.boneIds = glm::ivec4(-1, -1, -1, -1);
        vertex.boneWeights = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    
    // Process each bone
    for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {
        aiBone* bone = mesh->mBones[boneIndex];
        std::string boneName = bone->mName.C_Str();
        
        // Create bone object
        glm::mat4 offsetMatrix = Tools::convertAiToGLM(bone->mOffsetMatrix);
        auto assimpBone = std::make_shared<AssimpBone>(boneIndex, boneName, offsetMatrix);
        mBoneList.push_back(assimpBone);
        
        // Apply bone weights to vertices
        for (unsigned int weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex) {
            const aiVertexWeight& weight = bone->mWeights[weightIndex];
            unsigned int vertexId = weight.mVertexId;
            float weightValue = weight.mWeight;
            
            if (vertexId >= mMesh.vertices.size()) {
                Logger::log(0, "AssimpMesh: Vertex ID %d out of range for mesh '%s'\n", vertexId, mMeshName.c_str());
                continue;
            }
            
            // Find an empty slot in the vertex's bone data
            AnimatedVertex& vertex = mMesh.vertices[vertexId];
            for (int i = 0; i < 4; ++i) {
                if (vertex.boneIds[i] == -1) {
                    vertex.boneIds[i] = boneIndex;
                    vertex.boneWeights[i] = weightValue;
                    break;
                }
            }
        }
    }
    
    // Normalize bone weights for each vertex
    for (auto& vertex : mMesh.vertices) {
        float totalWeight = vertex.boneWeights.x + vertex.boneWeights.y + vertex.boneWeights.z + vertex.boneWeights.w;
        if (totalWeight > 0.0f) {
            vertex.boneWeights /= totalWeight;
        }
    }
}

std::string AssimpMesh::getMeshName() {
    return mMeshName;
}

unsigned int AssimpMesh::getTriangleCount() {
    return mTriangleCount;
}

unsigned int AssimpMesh::getVertexCount() {
    return mVertexCount;
}

VkMesh AssimpMesh::getMesh() {
    return mMesh;
}

std::vector<uint32_t> AssimpMesh::getIndices() {
    return mMesh.indices;
}

std::vector<std::shared_ptr<AssimpBone>> AssimpMesh::getBoneList() {
    return mBoneList;
}

} // namespace aveng
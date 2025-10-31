#include "AssimpMesh.h"
#include "Tools.h"
#include <climits>
#include <cmath>

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

        // glm::vec3 pos = Tools::convertAiToGLM(mesh->mVertices[i], false);  // DISABLE CONVERSION
        vertex.position = glm::vec4(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z, 0.0f);  // texCoord.x will be set below

        //  FIXED: Normal.xyz + unused .w
        if (mesh->HasNormals()) {
            // glm::vec3 norm = Tools::convertAiToGLM(mesh->mNormals[i], false);     // DISABLE CONVERSION
            vertex.normal = glm::vec4(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z, 0.0f);  // texCoord.y will be set below
        }
        
        //  FIXED: Texture coordinates packed into position.w and normal.w
        if (mesh->HasTextureCoords(0)) {
            vertex.position.w = mesh->mTextureCoords[0][i].x;  // texCoord.x → position.w
            vertex.normal.w = mesh->mTextureCoords[0][i].y;
        }
        
        //  FIXED: Default color.xyz + texCoord.y in .w
        glm::vec3 defaultColor = glm::vec3(1.0f, 1.0f, 1.0f);
        vertex.color = glm::vec4(defaultColor, 1.f);
        
        mMesh.vertices.emplace_back(vertex);
    }
    
    // Process indices
    for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
        const aiFace& face = mesh->mFaces[i];
        mMesh.indices.push_back(face.mIndices[0]);
        mMesh.indices.push_back(face.mIndices[1]);
        mMesh.indices.push_back(face.mIndices[2]);
    }
    
    // Process bone weights
    if (mesh->HasBones() > 0) {
        mMesh.hasAnimationData = true;
        processBoneWeights(mesh);
        Logger::log(1, "AssimpMesh: Processed %d bones for mesh '%s'\n", mesh->mNumBones, mMeshName.c_str());
    } else {
        // CRITICAL: This mesh has no bone influences!
        Logger::log(0, " MESH '%s' HAS NO BONE WEIGHTS! Will remain at bind pose during animation!\n", mMeshName.c_str());
        mMesh.hasAnimationData = false;
    }
    
    // DEBUGGING: Validate mesh data for deformation issues
    validateMeshData();
    
    // NEW: Detailed vertex comparison logging for first few vertices
    debugVertexData();
    
    Logger::log(1, "AssimpMesh: Successfully processed mesh '%s'\n", mMeshName.c_str());
    return true;
}

void AssimpMesh::validateMeshData() {
    Logger::log(1, "=== MESH VALIDATION: %s ===\n", mMeshName.c_str());
    
    // 1. Vertex position validation
    glm::vec3 minPos(FLT_MAX), maxPos(-FLT_MAX);
    int invalidPositions = 0;
    
    for (const auto& vertex : mMesh.vertices) {
        // Check for invalid positions (NaN, infinity)
        if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) || !std::isfinite(vertex.position.z)) {
            invalidPositions++;
            continue;
        }
        minPos = glm::min(minPos, glm::vec3(vertex.position));  // Extract .xyz from vec4
        maxPos = glm::max(maxPos, glm::vec3(vertex.position));  // Extract .xyz from vec4
    }
    
    Logger::log(1, "Position bounds: min(%.2f,%.2f,%.2f) max(%.2f,%.2f,%.2f)\n",
                minPos.x, minPos.y, minPos.z, maxPos.x, maxPos.y, maxPos.z);
    if (invalidPositions > 0) {
        Logger::log(0, "Error: Found %d vertices with invalid positions!\n", invalidPositions);
    }
    
    // 2. Normal validation
    int invalidNormals = 0, zeroNormals = 0;
    for (const auto& vertex : mMesh.vertices) {
        if (!std::isfinite(vertex.normal.x) || !std::isfinite(vertex.normal.y) || !std::isfinite(vertex.normal.z)) {
            invalidNormals++;
        } else if (glm::length(glm::vec3(vertex.normal)) < 0.1f) {  // Extract .xyz from vec4
            zeroNormals++;
        }
    }
    
    if (invalidNormals > 0) Logger::log(0, "Error: Found %d invalid normals!\n", invalidNormals);
    if (zeroNormals > 0) Logger::log(1, "Error: Found %d zero-length normals\n", zeroNormals);
    
    // 3. UV coordinate validation (now packed in position.w and color.w)
    int invalidUVs = 0;
    for (const auto& vertex : mMesh.vertices) {
        if (!std::isfinite(vertex.position.w) || !std::isfinite(vertex.color.w)) {  // UV from .w components
            invalidUVs++;
        }
    }
    if (invalidUVs > 0) Logger::log(0, "Error: Found %d invalid UV coordinates!\n", invalidUVs);
    
    // 4. Bone weight validation (for animated meshes)
    if (mMesh.hasAnimationData) {
        int invalidWeights = 0, unboundVertices = 0, exceededBoneLimit = 0;
        
        for (const auto& vertex : mMesh.vertices) {
            float totalWeight = vertex.boneWeight.x + vertex.boneWeight.y + 
                              vertex.boneWeight.z + vertex.boneWeight.w;
            
            // Check weight normalization
            if (abs(totalWeight - 1.0f) > 0.01f && totalWeight > 0.0f) {
                invalidWeights++;
            }
            
            // Check for unbound vertices (no bone influences)
            if (totalWeight == 0.0f) {
                unboundVertices++;
            }
            
            // Check for invalid bone indices
            for (int i = 0; i < 4; ++i) {
                if (vertex.boneNumber[i] >= static_cast<int>(mBoneList.size()) && vertex.boneNumber[i] != -1) {
                    exceededBoneLimit++;
                    break;
                }
            }
        }
        
        Logger::log(1, " ANIMATED MESH: %d bones, %d invalid weights, %d unbound vertices, %d invalid bone indices\n", 
                    static_cast<int>(mBoneList.size()), invalidWeights, unboundVertices, exceededBoneLimit);
        
        if (unboundVertices > 0) {
            Logger::log(0, "  %d vertices have no bone influences! (will remain at bind pose)\n", unboundVertices);
        }
        if (exceededBoneLimit > 0) {
            Logger::log(0, "  %d vertices reference invalid bone indices!\n", exceededBoneLimit);
        }
    } else {
        Logger::log(1, " STATIC MESH: No animation data - will not follow bone transforms\n");
    }
    
    // 5. Index buffer validation
    int invalidIndices = 0;
    for (const auto& index : mMesh.indices) {
        if (index >= mVertexCount) {
            invalidIndices++;
        }
    }
    if (invalidIndices > 0) {
        Logger::log(0, "  Found %d indices out of range!\n", invalidIndices);
    }
    
    Logger::log(1, "=== VALIDATION COMPLETE ===\n");
}

void AssimpMesh::debugVertexData() {
    Logger::log(1, "=== VERTEX DATA COMPARISON: %s ===\n", mMeshName.c_str());
    
    // Print first 3 vertices for detailed comparison
    size_t maxVertices = std::min(size_t(3), mMesh.vertices.size());
    
    for (size_t i = 0; i < maxVertices; ++i) {
        const auto& v = mMesh.vertices[i];
        Logger::log(1, "Vertex %zu:\n", i);
        Logger::log(1, "  Position: (%.6f, %.6f, %.6f)\n", v.position.x, v.position.y, v.position.z);
        Logger::log(1, "  Normal:   (%.6f, %.6f, %.6f)\n", v.normal.x, v.normal.y, v.normal.z);
        Logger::log(1, "  TexCoord: (%.6f, %.6f) [packed in .w components]\n", v.position.w, v.color.w);
        Logger::log(1, "  Color:    (%.6f, %.6f, %.6f)\n", v.color.x, v.color.y, v.color.z);
        if (mMesh.hasAnimationData) {
            Logger::log(1, "  BoneIds:  (%d, %d, %d, %d)\n", 
                        v.boneNumber.x, v.boneNumber.y, v.boneNumber.z, v.boneNumber.w);
            Logger::log(1, "  Weights:  (%.6f, %.6f, %.6f, %.6f)\n", 
                        v.boneWeight.x, v.boneWeight.y, v.boneWeight.z, v.boneWeight.w);
        }
    }
    
    // Print first 9 indices
    size_t maxIndices = std::min(size_t(9), mMesh.indices.size());
    Logger::log(1, "First %zu indices: ", maxIndices);
    for (size_t i = 0; i < maxIndices; ++i) {
        Logger::log(1, "%u ", mMesh.indices[i]);
    }
    Logger::log(1, "\n");
    
    // Check for obviously wrong data
    bool hasNegativeIndices = false;
    bool hasOutOfRangeIndices = false;
    int hasZeroPositions = 0;
    
    for (const auto& index : mMesh.indices) {
        if (index >= mVertexCount) hasOutOfRangeIndices = true;
    }
    
    for (const auto& vertex : mMesh.vertices) {
        if (vertex.position.x == 0.0f && vertex.position.y == 0.0f && vertex.position.z == 0.0f) {
            hasZeroPositions++;
        }
    }
    
    Logger::log(1, "Data quality check:\n");
    Logger::log(1, "  Out of range indices: %s\n", hasOutOfRangeIndices ? "YES (❌ CRITICAL ERROR)" : "No");
    Logger::log(1, "  Zero positions: %d vertices\n", hasZeroPositions);
    Logger::log(1, "=== VERTEX DEBUG COMPLETE ===\n");
}

void AssimpMesh::processBoneWeights(aiMesh* mesh) {
    // Initialize all bone data to defaults
    //for (auto& vertex : mMesh.vertices) {
    //    vertex.boneNumber = glm::ivec4(-1, -1, -1, -1);
    //    vertex.boneWeight = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    //}
    
    // Process each bone
    for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {

        aiBone* bone = mesh->mBones[boneIndex];
        std::string boneName = bone->mName.C_Str();
        unsigned int numWeights = bone->mNumWeights;

        Logger::log(1, "%s: --- bone nr. %i has name %s, contains %i weights\n", __FUNCTION__, boneIndex, boneName.c_str(), numWeights);

        // Create bone object
        glm::mat4 offsetMatrix = Tools::convertAiToGLM(bone->mOffsetMatrix);
        std::shared_ptr<AssimpBone> newBone = std::make_shared<AssimpBone>(boneIndex, boneName, offsetMatrix);
        mBoneList.push_back(newBone);
        
        // Apply bone weights to vertices
        for (unsigned int weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex) {

            const aiVertexWeight& weight = bone->mWeights[weightIndex];
            unsigned int vertexId = weight.mVertexId;
            float vertexWeight = weight.mWeight;
            
            if (vertexId >= mMesh.vertices.size()) {
                Logger::log(0, "AssimpMesh: Vertex ID %d out of range for mesh '%s'\n", vertexId, mMeshName.c_str());
                continue;
            }
            
            // Find an empty slot in the vertex's bone data
            // AnimatedVertex& vertex = mMesh.vertices[vertexId];

            glm::uvec4 currentIds = mMesh.vertices.at(vertexId).boneNumber;
            glm::vec4 currentWeights = mMesh.vertices.at(vertexId).boneWeight;

            for (int i = 0; i < 4; ++i) {
                if (currentWeights[i] == 0.0f) {
                    currentIds[i] = boneIndex;
                    currentWeights[i] = vertexWeight;
                    break;
                }
            }
            mMesh.vertices.at(vertexId).boneNumber = currentIds;
            mMesh.vertices.at(vertexId).boneWeight = currentWeights;
        }
    }
    
    // Normalize bone weights for each vertex
    //for (auto& vertex : mMesh.vertices) {
    //    float totalWeight = vertex.boneWeights.x + vertex.boneWeights.y + vertex.boneWeights.z + vertex.boneWeights.w;
    //    if (totalWeight > 0.0f) {
    //        vertex.boneWeights /= totalWeight;
    //    }
    //}
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
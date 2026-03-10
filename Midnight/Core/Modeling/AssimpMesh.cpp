#include "AssimpMesh.h"
#include "Utils/AssetResolution.h"
#include "CoreVK/Resources/MTexture.h"
#include "Core/Imaging/TextureRegistry.h"
#include "Core/Imaging/TextureGltfSource.h"
#include <iostream>

#include "Tools.h"
namespace aveng {

    bool AssimpMesh::processMesh(
        const VkRenderData& renderData, 
        EngineDevice& engineDevice, 
        aiMesh* mesh, 
        const aiScene* scene, 
        const std::string modelBaseDir,
        const std::string contentRoot, 
        std::unordered_map<std::string, VkTextureData>& textures,
        TextureRegistry& texReg,
        TextureGltfSource& gltfSrc, // No need to use the abstract base here
        int frameIndex
    ) {
        mMeshName = mesh->mName.C_Str();
        mTriangleCount = mesh->mNumFaces;
        mVertexCount = mesh->mNumVertices;


        /* Other Checks I should probably make use of in the future*/
        
        //for (unsigned int i = 0; i < AI_MAX_NUMBER_OF_COLOR_SETS; ++i) {
        //    if (mesh->HasVertexColors(i)) {
        //        // std::printf("%s: --- mesh has vertex colors in set %i\n", __FUNCTION__, i);
        //    }
        //}
        //if (mesh->HasNormals()) {
        //    // std::printf("%s: --- mesh has normals\n", __FUNCTION__);
        //}
        //for (unsigned int i = 0; i < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++i) {
        //    if (mesh->HasTextureCoords(i)) {
        //        // std::printf("%s: --- mesh has texture coords in set %i\n", __FUNCTION__, i);
        //    }
        //}

        aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
        if (material) {
            aiString materialName = material->GetName();

            bool texturesFound = false;
            if (mesh->mMaterialIndex >= 0) {
                // scan only for diffuse and scalar textures for a start
                // @TODO : aiTextureType_BASE_COLOR
                std::vector<aiTextureType> supportedTexTypes = { aiTextureType_DIFFUSE, aiTextureType_SPECULAR };
                for (const auto& texType : supportedTexTypes) {
                    unsigned int textureCount = material->GetTextureCount(texType);
                    if (textureCount > 0) {
                        
                        for (unsigned int i = 0; i < textureCount; ++i) {

                            aiString textureName;
                            material->GetTexture(texType, i, &textureName);
                        
                            std::string texName = textureName.C_Str();

                            // external textures referenced by materials (NOT embedded "*0")
                            if (!texName.empty() && texName.rfind("*", 0) != 0) {

                                // Resolve relative references robustly
                                std::string texPath = resolveModelTexturePath(modelBaseDir, contentRoot, texName);

                                TextureAssetKey newKey{ texPath };
                                TextureCreateRequest t_req;
                                t_req.assetKey = newKey;
                                t_req.debugName = "[Mesh Reference]" + newKey.value;
                                t_req.assimp_data = nullptr;

                                texReg.getOrCreate(newKey, gltfSrc, t_req, frameIndex);

                            }
                        }
                    }
                }
            }

            aiColor4D baseColor(0.0f, 0.0f, 0.0f, 1.0f);
            if (material->Get(AI_MATKEY_COLOR_DIFFUSE, baseColor) == aiReturn_SUCCESS && !texturesFound) {
                mBaseColor = glm::vec4(baseColor.r, baseColor.g, baseColor.b, baseColor.a);
                mMesh.usesPBRColors = true;
            }
        }

        glm::mat4 B = Tools::gltfToEngine;

        // Packing Loop
        for (unsigned int i = 0; i < mVertexCount; ++i) {

            glm::vec3 p_gltf(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z);
            glm::vec3 p_eng = glm::vec3(B * glm::vec4(p_gltf, 1.0f));

            VkVertex vertex;
            vertex.position.x = p_eng.x;
            vertex.position.y = p_eng.y;
            vertex.position.z = p_eng.z;

            if (mesh->HasVertexColors(0)) {
                vertex.color.r = mesh->mColors[0][i].r;
                vertex.color.g = mesh->mColors[0][i].g;
                vertex.color.b = mesh->mColors[0][i].b;
                vertex.color.a = mesh->mColors[0][i].a;
            }
            else {
                if (mMesh.usesPBRColors) {
                    vertex.color = mBaseColor;
                }
                else {
                    vertex.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                }
            }

            if (mesh->HasNormals()) {

                glm::vec3 n_gltf(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);
                glm::vec3 n_eng = glm::normalize(glm::vec3(B * glm::vec4(n_gltf, 0.0f)));

                vertex.normal.x = n_eng.x;
                vertex.normal.y = n_eng.y;
                vertex.normal.z = n_eng.z;
            }
            else {
                vertex.normal = glm::vec4(0.0f);
            }

            if (mesh->HasTextureCoords(0)) {
                vertex.position.w = mesh->mTextureCoords[0][i].x;
                vertex.normal.w = mesh->mTextureCoords[0][i].y;
            }
            else {
                vertex.position.w = 0.0f;
                vertex.normal.w = 0.0f;
            }

            mMesh.vertices.emplace_back(vertex);
        }

        // Packing Loop
        for (unsigned int i = 0; i < mTriangleCount; ++i) {
            aiFace face = mesh->mFaces[i];
            
            // Skip faces with fewer than 3 indices
            if (face.mNumIndices < 3) {
                std::printf("%s warning: mesh '%s' face %u has only %u indices, skipping\n", 
                    __FUNCTION__, mMeshName.c_str(), i, face.mNumIndices);
                continue;
            }
            
            // Validate each index is within vertex bounds
            bool validFace = true;
            for (unsigned int j = 0; j < 3; ++j) {
                if (face.mIndices[j] >= mVertexCount) {
                    std::printf("%s error: mesh '%s' face %u has out-of-bounds vertex index %u (vertex count: %u), skipping face\n",
                        __FUNCTION__, mMeshName.c_str(), i, face.mIndices[j], mVertexCount);
                    validFace = false;
                    break;
                }
            }
            
            // Valid
            if (validFace) {
                mMesh.indices.push_back(face.mIndices[0]);
                mMesh.indices.push_back(face.mIndices[1]);
                mMesh.indices.push_back(face.mIndices[2]);
            }
        }

        if (mesh->HasBones()) {

            const glm::mat4 B = Tools::gltfToEngine;

            unsigned int numBones = mesh->mNumBones;
            
            for (unsigned int boneId = 0; boneId < numBones; ++boneId) {

                std::string boneName = mesh->mBones[boneId]->mName.C_Str();
                unsigned int numWeights = mesh->mBones[boneId]->mNumWeights;

                glm::mat4 offset_gltf = Tools::convertAiToGLM(mesh->mBones[boneId]->mOffsetMatrix);
                // Use the sandwich (B * M * inv(B)), not just B * M.
                glm::mat4 offset_engine = B * offset_gltf * glm::inverse(B);

                //auto printScale = [](const char* tag, const glm::mat4& m) {
                //    glm::vec3 sx = glm::vec3(m[0]);
                //    glm::vec3 sy = glm::vec3(m[1]);
                //    glm::vec3 sz = glm::vec3(m[2]);
                //    printf("%s scale=(%.3f,%.3f,%.3f) det=%.3f\n",
                //        tag, glm::length(sx), glm::length(sy), glm::length(sz), glm::determinant(m));
                //};

                //printScale("offset_gltf ", offset_gltf);
                //printScale("offset_engine", offset_engine);

                std::shared_ptr<AssimpBone> newBone = std::make_shared<AssimpBone>(boneId, boneName, offset_engine);

                mBoneList.push_back(newBone);

                for (unsigned int weight = 0; weight < numWeights; ++weight) {
                    unsigned int vertexId = mesh->mBones[boneId]->mWeights[weight].mVertexId;
                    float vertexWeight = mesh->mBones[boneId]->mWeights[weight].mWeight;

                    glm::uvec4 currentIds = mMesh.vertices.at(vertexId).boneNumber;
                    glm::vec4 currentWeights = mMesh.vertices.at(vertexId).boneWeight;

                    /* insert weight and bone id into first free slot (weight => 0.0f) */
                    for (unsigned int i = 0; i < 4; ++i) {
                        if (currentWeights[i] == 0.0f) {
                            currentIds[i] = boneId;
                            currentWeights[i] = vertexWeight;

                            /* skip to next weight */
                            break;
                        }
                    }
                    mMesh.vertices.at(vertexId).boneNumber = currentIds;
                    mMesh.vertices.at(vertexId).boneWeight = currentWeights;
                }
            }
        }

        return true;
    }

    std::vector<uint32_t> AssimpMesh::getIndices() {
        return mMesh.indices;
    }

    VkMesh AssimpMesh::getMesh() {
        return mMesh;
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

    std::vector<std::shared_ptr<AssimpBone>> AssimpMesh::getBoneList() {
        return mBoneList;
    }
}
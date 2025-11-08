#include <cmath>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include "aveng_model.h"
#include "Utils/aveng_utils.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader/tiny_obj_loader.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
#include "data.h"

namespace std {

	// This function allows us to take a vertex struct instance and hash it, for use by an unordered map key
	// This allows us to create vertex buffers which only contain unique vertices
	template<>
	struct hash<aveng::VkVertex> {
		size_t operator()(aveng::VkVertex const& vertex) const {
	
			// for final hash value
			size_t seed = 0;
			aveng::hashCombine(seed, vertex.position, vertex.color, vertex.normal, vertex.boneNumber, vertex.boneWeight);
			return seed;
	
		}
	};
}

namespace aveng {

	struct Model {
		tinyobj::attrib_t attrib;
		std::vector<tinyobj::shape_t> shapes;
		std::vector<tinyobj::material_t> materials;
	};

	AvengModel::AvengModel(EngineDevice& device, VkRenderData& renderData, const std::string& filepath) 
		: engineDevice{ device },
		  mBoneParentMatrixBuffers(SwapChain::MAX_FRAMES_IN_FLIGHT),
		  mShaderBoneMatrixOffsetBuffers(SwapChain::MAX_FRAMES_IN_FLIGHT)
	{
		loadModelV2(renderData, filepath);
	}

	AvengModel::~AvengModel() {}

	void AvengModel::drawInstancedV2(VkRenderData& renderData, uint32_t instanceCount, int frameIndex) {
		for (unsigned int i = 0; i < mModelMeshes.size(); ++i) {
			VkMesh& mesh = mModelMeshes.at(i);

			// find diffuse texture by name
			VkTextureData diffuseTex{};
			auto diffuseTexName = mesh.textures.find(aiTextureType_DIFFUSE);
			if (diffuseTexName != mesh.textures.end()) {
				auto diffuseTexture = mTextures.find(diffuseTexName->second);
				if (diffuseTexture != mTextures.end()) {
					diffuseTex = diffuseTexture->second;
				}
			}

			/* switch between animated and non-animated pipeline layout */
			VkPipelineLayout renderLayout;
			if (hasAnimations()) {
				renderLayout = renderData.rdAvengAnimationPipelineLayout;
			}
			else {
				renderLayout = renderData.rdAvengPipelineLayout;
			}

			if (diffuseTex.image != VK_NULL_HANDLE) {
				vkCmdBindDescriptorSets(renderData.rdCommandBuffersGraphics[frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
					renderLayout, 0, 1, &diffuseTex.descriptorSet, 0, nullptr);
			}
			else {
				if (mesh.usesPBRColors) {
					vkCmdBindDescriptorSets(renderData.rdCommandBuffersGraphics[frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
						renderLayout, 0, 1, &mWhiteTexture.descriptorSet, 0, nullptr);
				}
				else {
					vkCmdBindDescriptorSets(renderData.rdCommandBuffersGraphics[frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
						renderLayout, 0, 1, &mPlaceholderTexture.descriptorSet, 0, nullptr);
				}
			}

			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(renderData.rdCommandBuffersGraphics[frameIndex], 0, 1, &mVertexBuffers.at(i).buffer, &offset);
			vkCmdBindIndexBuffer(renderData.rdCommandBuffersGraphics[frameIndex], mIndexBuffers.at(i).buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(renderData.rdCommandBuffersGraphics[frameIndex], static_cast<uint32_t>(mesh.indices.size()), instanceCount, 0, 0, 0);
		}
	}

	void AvengModel::bind(VkCommandBuffer commandBuffer)
	{
		VkBuffer buffers[] = { vertexBuffer->getBuffer() };
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);

		if (hasIndexBuffer) 
		{
			vkCmdBindIndexBuffer(commandBuffer, indexBuffer->getBuffer(), 0, VK_INDEX_TYPE_UINT32); // This index type can store up to 2^32 vertices
		}

	}

	std::string AvengModel::getModelFileName() {
		return mModelFilename;
	}

	std::string AvengModel::getModelFileNamePath() {
		return mModelFilenamePath;
	}


	bool AvengModel::loadModelV2(VkRenderData& renderData, const std::string& filepath, unsigned int extraImportFlags)
	{
		Assimp::Importer importer;

		// Essential flags for proper mesh loading and deformation debugging
		const aiScene* scene = importer.ReadFile(filepath,
			aiProcess_CalcTangentSpace |
			aiProcess_Triangulate |
			aiProcess_JoinIdenticalVertices |
			aiProcess_GenNormals |
			aiProcess_FlipUVs |                 //  ESSENTIAL: Flip V for Vulkan
			aiProcess_ValidateDataStructure |   // Validate mesh integrity
			aiProcess_SortByPType |
			// aiProcess_LimitBoneWeights |
			extraImportFlags
			// aiProcess_LimitBoneWeights |        // CRITICAL: Limit to 4 bones per vertex
			// aiProcess_GenSmoothNormals |        //  Generate normals if missing  
			// aiProcess_FixInfacingNormals |      //  Fix inverted normals
			// aiProcess_ImproveCacheLocality         
		);

		if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
			std::printf("AssimpModel: Error loading '%s': %s\n", filepath.c_str(), importer.GetErrorString());
			return false;
		}

		unsigned int numMeshes = scene->mNumMeshes;
		std::printf("AssimpModel: Found %d mesh%s in '%s'\n", numMeshes, numMeshes == 1 ? "" : "es", filepath.c_str());

		// Count vertices and faces
		for (unsigned int i = 0; i < numMeshes; ++i) {
			unsigned int numVertices = scene->mMeshes[i]->mNumVertices;
			unsigned int numFaces = scene->mMeshes[i]->mNumFaces;

			mVertexCount += numVertices;
			mTriangleCount += numFaces;

			std::printf("%s: mesh %i contains %i vertices and %i faces\n", __FUNCTION__, i, numVertices, numFaces);
		}
		std::printf("AssimpModel: Total %d vertices and %d faces\n", mVertexCount, mTriangleCount);

		aiNode* rootNode = scene->mRootNode;

		// Only for Embedded textures.
		if (scene->HasTextures()) {
			unsigned int numTextures = scene->mNumTextures;

			for (int i = 0; i < scene->mNumTextures; ++i) {
				std::string texName = scene->mTextures[i]->mFilename.C_Str();

				int height = scene->mTextures[i]->mHeight;
				int width = scene->mTextures[i]->mWidth;
				aiTexel* data = scene->mTextures[i]->pcData;

				VkTextureData newTex{};
				if (!Texture::loadTexture(engineDevice, renderData, newTex, texName, data, width, height)) {
					return false;
				}

				std::string internalTexName = "*" + std::to_string(i);
				std::printf("%s: - added internal texture '%s'\n", __FUNCTION__, internalTexName.c_str());
				mTextures.insert({ internalTexName, newTex });
			}

			std::printf("%s: scene has %i embedded textures\n", __FUNCTION__, numTextures);
		}

		/* add a white texture in case there is no diffuse tex but colors */
		std::string whiteTexName = "textures/white.png";
		if (!Texture::loadTexture(engineDevice, renderData, mWhiteTexture, whiteTexName)) {
			std::printf("%s error: could not load white default texture '%s'\n", __FUNCTION__, whiteTexName.c_str());
			return false;
		}

		/* add a placeholder texture in case there is no diffuse tex */
		std::string placeholderTexName = "textures/missing_tex.png";
		if (!Texture::loadTexture(engineDevice, renderData, mPlaceholderTexture, placeholderTexName)) {
			std::printf("%s error: could not load placeholder texture '%s'\n", __FUNCTION__, placeholderTexName.c_str());
			return false;
		}

		/* the textures are stored directly or relative to the model file */
		std::string assetDirectory = filepath.substr(0, filepath.find_last_of('/'));


		std::string rootNodeName = rootNode->mName.C_Str();
		mRootNode = AssimpNode::createNode(rootNodeName);
		std::printf("%s: root node name: '%s'\n", __FUNCTION__, rootNodeName.c_str());

		processNode(renderData, mRootNode, rootNode, scene, assetDirectory);

		/**
		  * Check your work
		  */
		for (const auto& entry : mNodeList) {
			std::vector<std::shared_ptr<AssimpNode>> childNodes = entry->getChilds();

			std::string parentName = entry->getParentNodeName();
			std::printf("%s: --- found node %s in node list, it has %i children, parent is %s\n", __FUNCTION__, entry->getNodeName().c_str(), childNodes.size(), parentName.c_str());

			for (const auto& node : childNodes) {
				std::printf("%s: ---- child: %s\n", __FUNCTION__, node->getNodeName().c_str());
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
			}
			else {
				boneParentIndexList.emplace_back(std::distance(mBoneList.begin(), boneIter));
			}
		}

		std::printf("%s: -- bone parents --\n", __FUNCTION__);
		for (unsigned int i = 0; i < mBoneList.size(); ++i) {
			std::printf("%s: bone %i (%s) has parent %i (%s)\n", __FUNCTION__, i, mBoneList.at(i)->getBoneName().c_str(), boneParentIndexList.at(i),
				boneParentIndexList.at(i) < 0 ? "invalid" : mBoneList.at(boneParentIndexList.at(i))->getBoneName().c_str());
		}
		std::printf("%s: -- bone parents --\n", __FUNCTION__);

		/* create vertex buffers for the meshes */
		for (const auto& mesh : mModelMeshes) {
			VkVertexBufferData vertexBuffer;
			VertexBuffer::init(engineDevice, vertexBuffer, mesh.vertices.size() * sizeof(VkVertex));
			VertexBuffer::uploadData(engineDevice, vertexBuffer, mesh);
			mVertexBuffers.emplace_back(vertexBuffer);

			VkIndexBufferData indexBuffer;
			IndexBuffer::init(engineDevice, indexBuffer, mesh.indices.size() * sizeof(uint32_t));
			IndexBuffer::uploadData(engineDevice, indexBuffer, mesh);
			mIndexBuffers.emplace_back(indexBuffer);
		}

		/* create descriptor set (for each available frame in flight) for per-model data */
		createDescriptorSet(renderData, boneOffsetMatricesList, boneParentIndexList);

		/* animations */
		unsigned int numAnims = scene->mNumAnimations;
		for (unsigned int i = 0; i < numAnims; ++i) {
			aiAnimation* animation = scene->mAnimations[i];

			std::printf("%s: -- animation clip %i has %i skeletal channels, %i mesh channels, and %i morph mesh channels\n",
				__FUNCTION__, i, animation->mNumChannels, animation->mNumMeshChannels, animation->mNumMorphMeshChannels);

			std::shared_ptr<AssimpAnimClip> animClip = std::make_shared<AssimpAnimClip>();
			animClip->addChannels(animation, mBoneList);
			if (animClip->getClipName().empty()) {
				animClip->setClipName(std::to_string(i));
			}
			mAnimClips.emplace_back(animClip);
		}

		mModelFilenamePath = filepath;
		mModelFilename = std::filesystem::path(filepath).filename().generic_string();

		/* get root transformation matrix from model's root node */
		mRootTransformMatrix = Tools::convertAiToGLM(rootNode->mTransformation);

		std::printf("%s: - model has a total of %zi bone%s\n", __FUNCTION__, mBoneList.size(), mBoneList.size() == 1 ? "" : "s");
		std::printf("%s: - model has a total of %zi animation%s\n", __FUNCTION__, numAnims, numAnims == 1 ? "" : "s");

		std::printf("%s: successfully loaded model '%s' (%s)\n", __FUNCTION__, filepath.c_str(), mModelFilename.c_str());
		return true;

	}

	bool AvengModel::createDescriptorSet(VkRenderData& renderData, std::vector<glm::mat4>& boneOffsetMatricesList, std::vector<int32_t>& boneParentIndexList) {

		/* init all SSBOs - These will take the current frame index into account, hence the vector usage */
		for (int i = 0; i < mBoneParentMatrixBuffers.size(); i++) {
			mBoneParentMatrixBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
				sizeof(boneParentIndexList.size()) * sizeof(int32_t),
				1,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VMA_MEMORY_USAGE_AUTO,
				1, // minOffsetAlignment
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

			mBoneParentMatrixBuffers[i]->map();
			mBoneParentMatrixBuffers[i]->writeToBuffer(boneParentIndexList.data());
		}

		for (int i = 0; i < mShaderBoneMatrixOffsetBuffers.size(); i++) {
			mShaderBoneMatrixOffsetBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
				sizeof(boneOffsetMatricesList.size()) * sizeof(glm::mat4),
				1,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VMA_MEMORY_USAGE_AUTO,
				1, // minOffsetAlignment
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

			mShaderBoneMatrixOffsetBuffers[i]->map();
			mShaderBoneMatrixOffsetBuffers[i]->writeToBuffer(boneOffsetMatricesList.data());
		}
		
		/* matrix multiplication, per-model data */
		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++)
		{
			auto parentNodeInfo = mBoneParentMatrixBuffers[i]->descriptorInfo(VK_WHOLE_SIZE, 0);
			auto boneOffsetInfo = mShaderBoneMatrixOffsetBuffers[i]->descriptorInfo(VK_WHOLE_SIZE, 0);
		
			// Basic Shader
			AvengDescriptorSetWriter(*renderData.rdAvengComputeMatrixMultPerModelDescriptorLayout, *renderData.avengDescriptorPool)
				.writeBuffer(0, &parentNodeInfo)
				.writeBuffer(1, &boneOffsetInfo)
				.build(mMatrixMultPerModelDescriptorSets[i]);

		}
		
		return true;
	}

	void AvengModel::processNode(VkRenderData& renderData, std::shared_ptr<AssimpNode> node, aiNode* aNode, const aiScene* scene, std::string assetDirectory) {
		std::string nodeName = aNode->mName.C_Str();
		std::printf("%s: node name: '%s'\n", __FUNCTION__, nodeName.c_str());

		unsigned int numMeshes = aNode->mNumMeshes;
		if (numMeshes > 0) {
			std::printf("%s: - node has %i meshes\n", __FUNCTION__, numMeshes);
			for (unsigned int i = 0; i < numMeshes; ++i) {
				aiMesh* modelMesh = scene->mMeshes[aNode->mMeshes[i]];

				AssimpMesh mesh;
				mesh.processMesh(renderData, engineDevice, modelMesh, scene, assetDirectory, mTextures);

				mModelMeshes.emplace_back(mesh.getMesh());

				/* avoid inserting duplicate bone Ids - meshes can reference the same bones */
				std::vector<std::shared_ptr<AssimpBone>> flatBones = mesh.getBoneList();
				for (const auto& bone : flatBones) {
					const auto iter = std::find_if(mBoneList.begin(), mBoneList.end(), [bone](std::shared_ptr<AssimpBone>& otherBone) { return bone->getBoneId() == otherBone->getBoneId(); });
					if (iter == mBoneList.end()) {
						mBoneList.emplace_back(bone);
					}
				}
			}
		}

		mNodeMap.insert({ nodeName, node });
		mNodeList.emplace_back(node);

		unsigned int numChildren = aNode->mNumChildren;
		std::printf("%s: - node has %i children \n", __FUNCTION__, numChildren);

		for (unsigned int i = 0; i < numChildren; ++i) {
			std::string childName = aNode->mChildren[i]->mName.C_Str();
			std::printf("%s: --- found child node '%s'\n", __FUNCTION__, childName.c_str());

			std::shared_ptr<AssimpNode> childNode = node->addChild(childName);
			processNode(renderData, childNode, aNode->mChildren[i], scene, assetDirectory);
		}
	}

	glm::mat4 AvengModel::getRootTranformationMatrix() {
		return mRootTransformMatrix;
	}

	const std::vector<std::shared_ptr<AssimpBone>>& AvengModel::getBoneList() {
		return mBoneList;
	}

	const std::vector<std::shared_ptr<AssimpAnimClip>>& AvengModel::getAnimClips() {
		return mAnimClips;
	}

	bool AvengModel::hasAnimations() {
		return !mAnimClips.empty();
	}

	unsigned int AvengModel::getTriangleCount() {
		return mTriangleCount;
	}

	const std::vector<std::unique_ptr<AvengBuffer>>& AvengModel::getBoneMatrixOffsetBuffers() const {
		return mShaderBoneMatrixOffsetBuffers;
	}

	const std::vector<std::unique_ptr<AvengBuffer>>& AvengModel::getBoneParentBuffers() const {
		return mBoneParentMatrixBuffers;
	}

	std::vector<VkDescriptorSet>& AvengModel::getMatrixMultDescriptorSets() {
		return mMatrixMultPerModelDescriptorSets;
	}

	VkDescriptorSet& AvengModel::getMatrixMultDescriptorSet(int frameIndex) {
		return mMatrixMultPerModelDescriptorSets[frameIndex];
	}

	void AvengModel::cleanup(EngineDevice& engineDevice, VkRenderData& renderData, int frames) {

		VkDescriptorPool pool = renderData.avengDescriptorPool->getPool();

		for (int i = 0; i < frames; i++) {
			vkFreeDescriptorSets(engineDevice.device(), pool, 1, &mMatrixMultPerModelDescriptorSets[i]);
		}

		for (auto buffer : mVertexBuffers) {
			VertexBuffer::cleanup(engineDevice, buffer);
		}
		for (auto buffer : mIndexBuffers) {
			IndexBuffer::cleanup(engineDevice, buffer);
		}

		//ShaderStorageBuffer::cleanup(renderData, mShaderBoneMatrixOffsetBuffer);
		//ShaderStorageBuffer::cleanup(renderData, mShaderBoneParentBuffer);

		for (auto& tex : mTextures) {
			Texture::cleanup(engineDevice, renderData, tex.second);
		}

		Texture::cleanup(engineDevice, renderData, mPlaceholderTexture);
		Texture::cleanup(engineDevice, renderData, mWhiteTexture);
	}


}
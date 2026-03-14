#pragma once

#include "avpch.h"
#include "Utils/AssetResolution.h"
#include "Core/Modeling/AssimpNode.h"
#include "Core/Modeling/AssimpMesh.h"
#include "Core/Modeling/AssimpAnimClip.h"
#include "Core/Modeling/Tools.h"
#include "Core/Asset/AssetRegistry.h"
#include "CoreVK/aveng_descriptors.h"
#include "CoreVK/VkRenderData.h"

#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>

namespace aveng {

	class EngineDevice;
	class TextureRegistry;

	class AvengModel 
	{

	public:

		/*
			This class caters to every type of 3D model that we load.
			That could become unwieldy but for now its not a
			priority to address this.
		*/

		AvengModel(EngineDevice& device);

		AvengModel(const AvengModel&) = delete;
		AvengModel& operator=(const AvengModel&) = delete;
		AvengModel(AvengModel&&) noexcept = default;
		AvengModel& operator=(AvengModel&&) noexcept = default;
		~AvengModel() = default;

		bool loadModelV3( // TODO - V4
			VkRenderData& renderData,
			const AssetKey& key,                // keep for debug + extension hint
			std::span<const std::byte> bytes,   // data from IAssetSource
			unsigned int extraImportFlags,		// Assimp flags
			TextureRegistry& texReg,		// New in V3
			const int frameIndex,
			const std::string& modelBaseDir = "",    // for model-owned refs
			const std::string& contentRoot = ""     // Texture root. for engine-owned defaults
		);

		bool build(
			VkRenderData& renderData,
			const AssetKey& key, // Not strictly needed
			const aiScene* scene,
			aiNode* rootNode,
			const int frameIndex
		);

		[[deprecated("Use V3 instead")]]
		bool buildV2(
			const VkRenderData& renderData,
			const AssetKey& key, // Not strictly needed
			const aiScene* scene,
			aiNode* rootNode);

		[[deprecated("Use V3 instead")]]
		bool loadModelV2(
			const VkRenderData& renderData,
			const AssetKey& key,                       // keep for debug + extension hint
			std::span<const std::byte> bytes,          // data from IAssetSource
			unsigned int extraImportFlags,
			const std::string& modelBaseDir = "",
			const std::string& contentRoot = ""
		);

		// bool loadModelV2(VkRenderData& renderData, const std::string& filepath, unsigned int extraImportFlags = 0);
		bool createDescriptorSet(const VkRenderData& renderData);
		void processNode(
			const VkRenderData& renderData, 
			std::shared_ptr<AssimpNode> node, 
			aiNode* aNode, 
			const aiScene* scene, 
			const std::string modelBaseDir, 
			const std::string contentRoot, // Texture root
			TextureRegistry& texReg,
			TextureGltfSource& gltfSrc, // No need to use the abstract base here
			int frameIndex
		/* std::string assetDirectory*/);

		std::string getModelFileName();
		std::string getModelFileNamePath();

		const std::vector<std::shared_ptr<AssimpBone>>& getBoneList();
		const std::vector<std::shared_ptr<AssimpBone>>& getBoneList() const;

		const std::vector<std::shared_ptr<AssimpAnimClip>>& getAnimClips();
		const std::vector<VkShaderStorageBufferData>& getBoneParentBuffers() const;

		//std::vector<VkDescriptorSet>& getMatrixMultDescriptorSets();
		//VkDescriptorSet& getMatrixMultDescriptorSet(int frameIndex);
		VkDescriptorSet getMatrixMultDescriptorSet(int frameIndex) const;

		glm::mat4 getRootTranformationMatrix();
		bool hasAnimations();
		unsigned int getTriangleCount();
		unsigned int getTriangleCount() const;

		std::vector<glm::mat4> getBoneOffsetMatricesList() { return boneOffsetMatricesList; }
		std::vector<int32_t> getBoneParentIndexList() { return boneParentIndexList; }

		void drawInstancedV2(VkCommandBuffer graphicsCommandBuffer, VkPipelineLayout pipelineLayout, uint32_t instanceCount, int frameIndex) const;
		void drawInstancedV3(VkCommandBuffer graphicsCommandBuffer, VkPipelineLayout bindlessLayout, uint32_t instanceCount, int frameIndex) const;

		void cleanup(EngineDevice& engineDevice, VkRenderData& renderData);
	
		std::string path; 

		// Important indices. Might want to relocate these, or otherwise avoid copying them carelessly to other destinations
		uint32_t nParentNodeIndices = 0;

	private:

		EngineDevice& engineDevice;
		bool hasIndexBuffer = false;

		// map textures to external or internal texture names
		std::unordered_map<std::string, VkTextureData> mTextures{}; // deprecated
		VkTextureData mPlaceholderTexture{}; // deprecated
		VkTextureData mWhiteTexture{}; // deprecated

		// GPU Data
		std::vector<VkMesh> mModelMeshes{};

		// GPU resource members
		std::vector<VkVertexBufferData> mVertexBuffers{};
		std::vector<VkIndexBufferData> mIndexBuffers{};

		// Compute data members - NOTE - These are ephemeral and will be destroyed after the model is loaded.
		// Their contents go to global buffers in VkRenderData
		std::vector<glm::mat4> boneOffsetMatricesList{};
		std::vector<int32_t> boneParentIndexList{};

		unsigned int mTriangleCount = 0;
		unsigned int mVertexCount = 0;

		/* store the root node for direct access */
		std::shared_ptr<AssimpNode> mRootNode = nullptr;

		std::vector<VkShaderStorageBufferData> mBoneParentMatrixBuffers; // DEPRECATED
		std::vector<VkShaderStorageBufferData> mShaderBoneMatrixOffsetBuffers; // DEPRECATED

		/* a map to find the node by name */
		std::unordered_map<std::string, std::shared_ptr<AssimpNode>> mNodeMap{}; // Shared ptr nonsense
		/* and a 'flat' map to keep the order of insertation  */
		std::vector<std::shared_ptr<AssimpNode>> mNodeList{}; // Shared ptr nonsense

		std::vector<std::shared_ptr<AssimpBone>> mBoneList; // Shared ptr nonsense

		std::vector<std::shared_ptr<AssimpAnimClip>> mAnimClips{}; // Shared ptr nonsense


		/*
		 FOR SoA REFACTOR
		std::vector<AssimpNode>     mNodes;
		std::unordered_map<std::string, uint32_t> mNodeIdByName;

		std::vector<AssimpBone>     mBones;
		std::unordered_map<std::string, uint32_t> mBoneIdByName;

		std::vector<AssimpAnimClip> mAnimClips;

		
		*/

		glm::mat4 mRootTransformMatrix = glm::mat4(1.0f);

		std::vector<VkDescriptorSet> mMatrixMultPerModelDescriptorSets; // DEPRECATED
		VkDescriptorSetLayout mComputeMatrixMultPerModelDescriptorSetLayout; // DEPRECATED

		std::string mModelFilenamePath{"nullpath"};
		std::string mModelFilename{"nullname"}; // Deprecatee this, or let the null instantiation set this name, not default to it

	};

} //
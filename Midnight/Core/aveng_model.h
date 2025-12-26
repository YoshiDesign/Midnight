#pragma once

#include <span>
#include <array>
#include <memory>
#include <vector>
#include "Utils/glm_includes.h"
#include "Utils/AssetResolution.h"
#include "Core/Modeling/AssimpNode.h"
#include "Core/Modeling/AssimpMesh.h"
#include "Core/Modeling/AssimpAnimClip.h"
#include "Core/Modeling/Tools.h"
#include "Core/Modeling/ModelRegistry.h"
#include "CoreVK/aveng_descriptors.h"
#include "CoreVK/VkRenderData.h"
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>

class EngineDevice;

namespace aveng {

	class AvengModel 
	{

	public:

		AvengModel(EngineDevice& device);

		~AvengModel();

		AvengModel(const AvengModel&) = delete;
		AvengModel& operator=(const AvengModel&) = delete;

		bool loadModelV2(
			VkRenderData& renderData,
			const AssetKey& key,                       // keep for debug + extension hint
			std::span<const std::byte> bytes,          // data from IModelSource
			unsigned int extraImportFlags,
			const std::string& modelBaseDir = "",
			const std::string& contentRoot = ""
		);

		bool loadModelV2(VkRenderData& renderData, const std::string& filepath, unsigned int extraImportFlags = 0);
		bool createDescriptorSet(VkRenderData& renderData);
		void processNode(
			VkRenderData& renderData, 
			std::shared_ptr<AssimpNode> node, 
			aiNode* aNode, 
			const aiScene* scene, 
			const std::string modelBaseDir, 
			const std::string contentRoot // Texture root
		/* std::string assetDirectory*/);

		std::string getModelFileName();
		std::string getModelFileNamePath();

		const std::vector<std::shared_ptr<AssimpBone>>& getBoneList();
		const std::vector<std::shared_ptr<AssimpAnimClip>>& getAnimClips();
		const std::vector<VkShaderStorageBufferData>& getBoneMatrixOffsetBuffers() const;
		const std::vector<VkShaderStorageBufferData>& getBoneParentBuffers() const;
		std::vector<VkDescriptorSet>& getMatrixMultDescriptorSets();
		VkDescriptorSet& getMatrixMultDescriptorSet(int frameIndex);
		glm::mat4 getRootTranformationMatrix();
		bool hasAnimations();
		unsigned int getTriangleCount();
		unsigned int getTriangleCount() const;

		void drawInstancedV2(VkRenderData& renderData, VkPipelineLayout basicLayout, VkPipelineLayout animationLayout, uint32_t instanceCount, int frameIndex);

		void cleanup(EngineDevice& engineDevice, VkRenderData& renderData, int frames);
	
		std::string path; 

	private:

		//void createVertexBuffers(const std::vector<Vertex>& vertices);
		//void createIndexBuffers(const std::vector<uint32_t>& indices);

		EngineDevice& engineDevice;
		bool hasIndexBuffer = false;

		// map textures to external or internal texture names
		std::unordered_map<std::string, VkTextureData> mTextures{};
		VkTextureData mPlaceholderTexture{};
		VkTextureData mWhiteTexture{};

		std::vector<VkMesh> mModelMeshes{};
		std::vector<VkVertexBufferData> mVertexBuffers{};
		std::vector<VkIndexBufferData> mIndexBuffers{};
		unsigned int mTriangleCount = 0;
		unsigned int mVertexCount = 0;

		/* store the root node for direct access */
		std::shared_ptr<AssimpNode> mRootNode = nullptr;

		std::vector<VkShaderStorageBufferData> mBoneParentMatrixBuffers;
		std::vector<VkShaderStorageBufferData> mShaderBoneMatrixOffsetBuffers;

		/* a map to find the node by name */
		std::unordered_map<std::string, std::shared_ptr<AssimpNode>> mNodeMap{};
		/* and a 'flat' map to keep the order of insertation  */
		std::vector<std::shared_ptr<AssimpNode>> mNodeList{};

		std::vector<std::shared_ptr<AssimpBone>> mBoneList;

		std::vector<std::shared_ptr<AssimpAnimClip>> mAnimClips{};

		glm::mat4 mRootTransformMatrix = glm::mat4(1.0f);

		std::vector<VkDescriptorSet> mMatrixMultPerModelDescriptorSets;
		VkDescriptorSetLayout mComputeMatrixMultPerModelDescriptorSetLayout;

		std::string mModelFilenamePath{"nullpath"};
		std::string mModelFilename{"nullname"}; // Deprecatee this, or let the null instantiation set this name, not default to it

	};

} //
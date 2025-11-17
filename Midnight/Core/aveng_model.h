#pragma once

#include "Utils/glm_includes.h"

#include <array>
#include <memory>
#include <vector>

#include "data.h"
#include "CoreVK/swapchain.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVK/aveng_buffer.h"
#include "CoreVK/VertexBuffer.h"
#include "CoreVK/IndexBuffer.h"
#include "CoreVK/aveng_descriptors.h"
#include "Core/Modeling/AssimpNode.h"
#include "Core/Modeling/AssimpMesh.h"
#include "Core/Modeling/AssimpAnimClip.h"
#include "CoreVK/VkRenderData.h"
#include "Core/Modeling/Tools.h"
#include "CoreVK/AvengStorageBuffer.h"

#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>

namespace aveng {

	class AvengModel 
	{

	public:

		//// Vertex and index information to be sent to the model's vertex and index buffer memory
		//struct Builder {
		//	std::vector<VkVertex> vertices{};
		//	std::vector<uint32_t> indices{};

		//	void loadModel(const std::string& filepath);
		//	
		//};

		// Deprecated
		// AvengModel(EngineDevice& device, std::vector<Vertex> vertices, std::vector<uint32_t> indices, const std::string& filepath);
		AvengModel(EngineDevice& device);

		~AvengModel();

		AvengModel(const AvengModel&) = delete;
		AvengModel& operator=(const AvengModel&) = delete;

		bool loadModelV2(VkRenderData& renderData, const std::string& filepath, unsigned int extraImportFlags = 0);
		bool createDescriptorSet(VkRenderData& renderData);
		void processNode(VkRenderData& renderData, std::shared_ptr<AssimpNode> node, aiNode* aNode, const aiScene* scene, std::string assetDirectory);

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

		void drawInstancedV2(VkRenderData& renderData, uint32_t instanceCount, int frameIndex);

		void cleanup(EngineDevice& engineDevice, VkRenderData& renderData, int frames);
	
		std::string path; 

	private:

		//void createVertexBuffers(const std::vector<Vertex>& vertices);
		//void createIndexBuffers(const std::vector<uint32_t>& indices);

		EngineDevice& engineDevice;
		bool hasIndexBuffer = false;
		uint32_t vertexCount; // Old
		uint32_t indexCount; // Old

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

		std::string mModelFilenamePath;
		std::string mModelFilename;

	};

} //
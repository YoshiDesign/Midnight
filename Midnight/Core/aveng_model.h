#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

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

		// Vertex and index information to be sent to the model's vertex and index buffer memory
		struct Builder {
			std::vector<Vertex> vertices{};
			std::vector<uint32_t> indices{};

			void loadModel(const std::string& filepath);
			
		};

		//AvengModel(EngineDevice& device, const AvengModel::Builder& builder);
		AvengModel(EngineDevice& device, std::vector<Vertex> vertices, std::vector<uint32_t> indices, const std::string& filepath);
		AvengModel(EngineDevice& device, VkRenderData& renderData, const std::string& filepath);

		~AvengModel();

		AvengModel(const AvengModel&) = delete;
		AvengModel& operator=(const AvengModel&) = delete;

		bool loadModelV2(VkRenderData& renderData, const std::string& filepath, unsigned int extraImportFlags = 0);
		bool createDescriptorSet(VkRenderData& renderData, std::vector<glm::mat4>& boneOffsetMatricesList, std::vector<int32_t>& boneParentIndexList);
		void processNode(VkRenderData& renderData, std::shared_ptr<AssimpNode> node, aiNode* aNode, const aiScene* scene/*, std::string assetDirectory*/);

		const std::vector<std::shared_ptr<AssimpBone>>& getBoneList();
		const std::vector<std::shared_ptr<AssimpAnimClip>>& getAnimClips();
		std::vector<std::unique_ptr<AvengBuffer>> getBoneMatrixOffsetBuffers();
		std::vector<std::unique_ptr<AvengBuffer>> getBoneParentBuffers();
		std::vector<VkDescriptorSet>& getMatrixMultDescriptorSets();
		VkDescriptorSet& getMatrixMultDescriptorSet(int frameIndex);
		glm::mat4 getRootTranformationMatrix();
		bool hasAnimations();
		unsigned int getTriangleCount();

		static std::unique_ptr<AvengModel> createModelFromFile(EngineDevice& device, VkRenderData& renderData, const std::string& filepath);
		static std::unique_ptr<AvengModel> drawTriangle(EngineDevice& device, glm::vec3 pos, const std::string& filepath);
		
		void bind(VkCommandBuffer commandBuffer);
		void bindInstanced(VkCommandBuffer commandBuffer, VkBuffer instanceBuffer);
		void draw(VkCommandBuffer commandBuffer);
		void drawInstancedV2(VkRenderData& renderData, uint32_t instanceCount, int frameIndex);
		void drawInstancedOLD(VkCommandBuffer commandBuffer, uint32_t instanceCount, uint32_t firstInstance);
		
		// Static methods for instance rendering setup
		static std::vector<VkVertexInputBindingDescription> getInstancedBindingDescriptions();
		static std::vector<VkVertexInputAttributeDescription> getInstancedAttributeDescriptions();


		static std::vector<VkVertexInputBindingDescription> getV2BindingDescriptions();
		static std::vector<VkVertexInputAttributeDescription> getV2AttributeDescriptions();
	
		std::string path; 

	private:

		void createVertexBuffers(const std::vector<Vertex>& vertices);
		void createIndexBuffers(const std::vector<uint32_t>& indices);

		EngineDevice& engineDevice;
		bool hasIndexBuffer = false;
		uint32_t vertexCount; // Old
		uint32_t indexCount; // Old

		unsigned int mVertexCount; // Old
		unsigned int mTriangleCount; // Old

		std::vector<VkMesh> mModelMeshes{};
		std::vector<VkVertexBufferData> mVertexBuffers{};
		std::vector<VkIndexBufferData> mIndexBuffers{};
		unsigned int mTriangleCount;

		/* store the root node for direct access */
		std::shared_ptr<AssimpNode> mRootNode = nullptr;

		std::vector<std::unique_ptr<AvengBuffer>> mBoneParentMatrixBuffers;
		std::vector<std::unique_ptr<AvengBuffer>> mShaderBoneMatrixOffsetBuffers;

		/* a map to find the node by name */
		std::unordered_map<std::string, std::shared_ptr<AssimpNode>> mNodeMap{};
		/* and a 'flat' map to keep the order of insertation  */
		std::vector<std::shared_ptr<AssimpNode>> mNodeList{};

		std::vector<std::shared_ptr<AssimpBone>> mBoneList;

		std::vector<std::shared_ptr<AssimpAnimClip>> mAnimClips{};

		glm::mat4 mRootTransformMatrix = glm::mat4(1.0f);

		std::vector<VkDescriptorSet> mMatrixMultPerModelDescriptorSets;
		VkDescriptorSetLayout mComputeMatrixMultPerModelDescriptorSetLayout;

		// NEW
		std::unique_ptr<AvengBuffer> vertexBuffer;

		// NEW
		std::unique_ptr<AvengBuffer> indexBuffer;

		std::string mModelFilenamePath;
		std::string mModelFilename;

	};

} //
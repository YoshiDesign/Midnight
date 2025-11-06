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
	struct hash<aveng::Vertex> {
		size_t operator()(aveng::Vertex const& vertex) const {
	
			// for final hash value
			size_t seed = 0;
			aveng::hashCombine(seed, vertex.position, vertex.color, vertex.normal, vertex.texCoord);
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

	AvengModel::AvengModel(EngineDevice& device, std::vector<Vertex> vertices, std::vector<uint32_t> indices, const std::string& filepath)
		: engineDevice{ device }
	{
		std::cout << "Instantiating Model..." << std::endl;
		createVertexBuffers(vertices);
		createIndexBuffers(indices);

		path = filepath;
		
		// Verify VMA allocation for model buffers
		std::cout << "  Model VMA Verification:" << std::endl;
		std::cout << "    Vertex Buffer using VMA: " << (vertexBuffer->isUsingVMA() ? "YES" : "NO") << std::endl;
		if (hasIndexBuffer) {
			std::cout << "    Index Buffer using VMA: " << (indexBuffer->isUsingVMA() ? "YES" : "NO") << std::endl;
		}
	}

	AvengModel::AvengModel(EngineDevice& device, VkRenderData& renderData, const std::string& filepath) 
		: engineDevice{ device }
	{
		loadModelV2(renderData, filepath);
	}

	AvengModel::~AvengModel() {}

	std::unique_ptr<AvengModel> AvengModel::createModelFromFile(EngineDevice& device, VkRenderData& renderData, const std::string& filepath)
	{
		// DEPRECATED
		//Builder builder{};
		//builder.loadModel(filepath);
		// return std::make_unique<AvengModel>(device, builder.vertices, builder.indices, filepath);

		// V2
		return std::make_unique<AvengModel>(device, renderData, filepath);
	}

	std::unique_ptr<AvengModel> AvengModel::drawTriangle(EngineDevice& device, glm::vec3 pos, const std::string& filepath)
	{
		std::vector<Vertex> vertices { // vector
			{ { pos.x, pos.y, pos.z }, {1.0f, 1.0f, 1.0f }, {1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
			{ { pos.x + 0.5f,  pos.y + 0.5f, pos.z }, {1.0f, 1.0f, 1.0f }, {1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
			{ { pos.x - 0.5f,  pos.y + 0.5f, pos.z }, {1.0f, 1.0f, 1.0f }, {1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } }
		};

		std::vector<uint32_t> indices = { 0,1,2 };
		return std::make_unique<AvengModel>(device, vertices, indices, filepath);
	}

	/**
	* TODO - Make sure these buffers get VMA allocated
		@function createVertexBuffers
		Create a vertex buffer in our device memory
		These buffers are used to write information to device memory
		- vkMapMemory maps a buffer on the host to a buffer on the device
	*/
	void AvengModel::createVertexBuffers(const std::vector<Vertex>& vertices)
	{
		vertexCount = static_cast<uint32_t>(vertices.size());
		assert(vertexCount >= 3 && "Vertex count must be at least 3");
		// Size of a vertex * number of vertices
		VkDeviceSize bufferSize = sizeof(vertices[0]) * vertexCount;
		uint32_t vertexSize = sizeof(vertices[0]);

		// Used to map data from the CPU to the GPU via staging buffer which will then copy the data to the device's optimal memory location
		AvengBuffer stagingBuffer{
			engineDevice,
			vertexSize,
			vertexCount,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VMA_MEMORY_USAGE_AUTO,
			1, // minOffsetAlignment
			VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
		};

		// This takes care of vkMapMemory -> memcpy(vertices.data() ...) -> vkUnmapMemory
		stagingBuffer.map();
		stagingBuffer.writeToBuffer((void*)vertices.data());

		vertexBuffer = std::make_unique<AvengBuffer>(
			engineDevice,
			vertexSize,
			vertexCount,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE // VMA automatically chooses fastest memory type available
		);

		// Copy memory from the staging buffer to the vertex buffer
		engineDevice.copyBuffer(stagingBuffer.getBuffer(), vertexBuffer->getBuffer(), bufferSize); // (src buffer, dst buffer, bufferSize)

	}

	/*
		Create an index buffer in our device memory
		These buffers are used to write information to device memory
		- vkMapMemory maps a buffer on the host to a buffer on the device
	*/
	void AvengModel::createIndexBuffers(const std::vector<uint32_t>& indices)
	{
		indexCount = static_cast<uint32_t>(indices.size());
		hasIndexBuffer = indexCount > 0;

		if (!hasIndexBuffer) return;

		// Size of a vertex * number of indices
		VkDeviceSize bufferSize = sizeof(indices[0]) * indexCount;
		uint32_t indexSize = sizeof(indices[0]);

		AvengBuffer stagingBuffer{
			engineDevice,
			indexSize,
			indexCount,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VMA_MEMORY_USAGE_AUTO,
			1, // minOffsetAlignment
			VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
		};

		stagingBuffer.map();
		stagingBuffer.writeToBuffer((void*)indices.data());

		indexBuffer = std::make_unique<AvengBuffer>(
			engineDevice,
			indexSize,
			indexCount,
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE // VMA automatically chooses fastest memory type available
		);

		engineDevice.copyBuffer(stagingBuffer.getBuffer(), indexBuffer->getBuffer(), bufferSize);

	}

	void AvengModel::draw(VkCommandBuffer commandBuffer) 
	{
		if (hasIndexBuffer) 
		{
			vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
		}
		else {
			vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);
		}
	}

	void AvengModel::drawInstancedOLD(VkCommandBuffer commandBuffer, uint32_t instanceCount, uint32_t firstInstance)
	{
		if (hasIndexBuffer)
		{
			vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, 0, 0, firstInstance);
		}
		else {
			vkCmdDraw(commandBuffer, vertexCount, instanceCount, 0, firstInstance);
		}
	}

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

	// DEPRECATED
	void AvengModel::bindInstanced(VkCommandBuffer commandBuffer, VkBuffer instanceBuffer)
	{
		// Bind vertex buffer at binding 0
		VkBuffer buffers[] = { vertexBuffer->getBuffer() };
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);

		// Bind instance buffer at binding 1
		VkBuffer instanceBuffers[] = { instanceBuffer };
		VkDeviceSize instanceOffsets[] = { 0 };
		vkCmdBindVertexBuffers(commandBuffer, 1, 1, instanceBuffers, instanceOffsets);

		if (hasIndexBuffer) 
		{
			vkCmdBindIndexBuffer(commandBuffer, indexBuffer->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
		}
	}

	//std::vector<VkVertexInputBindingDescription> AvengModel::getV2BindingDescriptions() {
	//	std::vector<VkVertexInputBindingDescription> bindingDescriptions(2);

	//	// Binding 0: Vertex data (per-vertex rate)
	//	bindingDescriptions[0].binding = 0;
	//	bindingDescriptions[0].stride = sizeof(VkVertex);
	//	bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	//	//// Binding 1: Instance data (per-instance rate)
	//	//bindingDescriptions[1].binding = 1;
	//	//bindingDescriptions[1].stride = sizeof(VkInstanceData);
	//	//bindingDescriptions[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

	//	return bindingDescriptions;
	//}

	//std::vector<VkVertexInputAttributeDescription> AvengModel::getV2AttributeDescriptions() {
	//	std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};

	//	// Per-vertex attributes from binding 0
	//	attributeDescriptions.push_back({ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VkVertex, position) });
	//	attributeDescriptions.push_back({ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VkVertex, color) });
	//	attributeDescriptions.push_back({ 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VkVertex, normal) });
	//	attributeDescriptions.push_back({ 3, 0, VK_FORMAT_R32G32B32_UINT, offsetof(VkVertex, boneNumber) });
	//	attributeDescriptions.push_back({ 4, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VkVertex, boneWeight) });

	//	// Per-instance attributes from binding 1
	//	// Instance modelMatrix (mat4 = 4 vec4s, so 4 locations: 5,6,7,8)
	//	for (uint32_t i = 0; i < 4; i++) {
	//		attributeDescriptions.push_back({
	//			5 + i,                                          // location
	//			1,                                              // binding
	//			VK_FORMAT_R32G32B32A32_SFLOAT,                 // format (vec4)
	//			static_cast<uint32_t>(offsetof(VkInstanceData, modelMatrix) + sizeof(glm::vec4) * i)
	//			});
	//	}

	//	// Instance normalMatrix (mat4 = 4 vec4s, so 4 locations: 9,10,11,12)
	//	for (uint32_t i = 0; i < 4; i++) {
	//		attributeDescriptions.push_back({
	//			8 + i,                                          // location
	//			1,                                              // binding
	//			VK_FORMAT_R32G32B32A32_SFLOAT,                 // format (vec4)
	//			static_cast<uint32_t>(offsetof(VkInstanceData, normalMatrix) + sizeof(glm::vec4) * i)
	//			});
	//	}

	//	// Instance textureIndex (int = location 13)
	//	attributeDescriptions.push_back({
	//		11,                                             // location
	//		1,                                              // binding
	//		VK_FORMAT_R32_SINT,                            // format (int)
	//		static_cast<uint32_t>(offsetof(VkInstanceData, textureIndex))
	//		});

	//	return attributeDescriptions;
	//}
	///*
	//* @function AvengModel::Vertex::getBindingDescriptions
	//* 1 of 2 requirements for describing how Vulkan
	//* should pass data into the vertex shader
	//*/
	//std::vector<VkVertexInputBindingDescription> Vertex::getBindingDescriptions()
	//{
	//	// This VkVertexInputBindingDescription corresponds to a single vertex buffer
	//	// it will occupy the binding at index 0.
	//	// The stride advances at sizeof(Vertex) bytes per vertex.
	//	// There is only 1 for now.
	//	/*
	//	    uint32_t             binding;
	//		uint32_t             stride;
	//		VkVertexInputRate    inputRate;
	//	*/
	//	std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
	//	bindingDescriptions[0].binding = 0;
	//	bindingDescriptions[0].stride = sizeof(Vertex);
	//	bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;		// Can be per vertex or per instance
	//	return bindingDescriptions;
	//}

	///*
	//* @function AvengModel::Vertex::getAttributeDescriptions
	//* 2 of 2 required functions for describing how Vulkan
	//* should pass data into the vertex shader
	//*/
	//std::vector<VkVertexInputAttributeDescription> Vertex::getAttributeDescriptions()
	//{
	//	 /*
	//		uint32_t    location;	-- This specifies the location as assigned in the vertex shader i.e. layout( location = 0 ) 
	//		uint32_t    binding;	
	//		VkFormat    format;		-- Datatype: 2 components each 32bit signed floats
	//		uint32_t    offset;		-- type, membername. Calculates the byte offset of the position member from the Vertex struct
	//	 */
	//	// return { {0, 0, VK_FORMAT_R32G32_SFLOAT, 0} };
	//	std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};

	//	attributeDescriptions.push_back({ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position) });		// Vertex Positions
	//	attributeDescriptions.push_back({ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color) });			// Vertex colors
	//	attributeDescriptions.push_back({ 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) });		// Defines a surface's normal (the non-culled side)
	//	attributeDescriptions.push_back({ 3, 0, VK_FORMAT_R32G32_SFLOAT,	offsetof(Vertex, texCoord) });		// Texture coordinates

	//	return attributeDescriptions;

	//}

	///*
	//* @function AvengModel::getInstancedBindingDescriptions
	//* Returns binding descriptions for instanced rendering
	//* Binding 0: Per-vertex data (position, color, normal, texCoord)
	//* Binding 1: Per-instance data (modelMatrix, normalMatrix, textureIndex)
	//*/
	//std::vector<VkVertexInputBindingDescription> AvengModel::getInstancedBindingDescriptions()
	//{
	//	std::vector<VkVertexInputBindingDescription> bindingDescriptions(2);
	//	
	//	// Binding 0: Vertex data (per-vertex rate)
	//	bindingDescriptions[0].binding = 0;
	//	bindingDescriptions[0].stride = sizeof(Vertex);
	//	bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	//	
	//	// Binding 1: Instance data (per-instance rate)
	//	bindingDescriptions[1].binding = 1;
	//	bindingDescriptions[1].stride = sizeof(InstanceData);
	//	bindingDescriptions[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
	//	
	//	return bindingDescriptions;
	//}

	///*
	//* @function AvengModel::getInstancedAttributeDescriptions
	//* Returns attribute descriptions for instanced rendering
	//* Locations 0-3: Per-vertex attributes
	//* Locations 4-11: Per-instance attributes (mat4 takes 4 locations each)
	//* Location 12: textureIndex
	//*/
	//std::vector<VkVertexInputAttributeDescription> AvengModel::getInstancedAttributeDescriptions()
	//{
	//	std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};

	//	// Per-vertex attributes from binding 0
	//	attributeDescriptions.push_back({ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position) });
	//	attributeDescriptions.push_back({ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color) });
	//	attributeDescriptions.push_back({ 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) });
	//	attributeDescriptions.push_back({ 3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, texCoord) });

	//	// Per-instance attributes from binding 1
	//	// Instance modelMatrix (mat4 = 4 vec4s, so 4 locations: 4, 5, 6, 7)
	//	for (uint32_t i = 0; i < 4; i++) {
	//		attributeDescriptions.push_back({
	//			4 + i,                                          // location
	//			1,                                              // binding
	//			VK_FORMAT_R32G32B32A32_SFLOAT,                 // format (vec4)
	//			static_cast<uint32_t>(offsetof(InstanceData, modelMatrix) + sizeof(glm::vec4) * i)
	//		});
	//	}

	//	// Instance normalMatrix (mat4 = 4 vec4s, so 4 locations: 8, 9, 10, 11)
	//	for (uint32_t i = 0; i < 4; i++) {
	//		attributeDescriptions.push_back({
	//			8 + i,                                          // location
	//			1,                                              // binding
	//			VK_FORMAT_R32G32B32A32_SFLOAT,                 // format (vec4)
	//			static_cast<uint32_t>(offsetof(InstanceData, normalMatrix) + sizeof(glm::vec4) * i)
	//		});
	//	}

	//	// Instance textureIndex (int = location 12)
	//	attributeDescriptions.push_back({
	//		12,                                             // location
	//		1,                                              // binding
	//		VK_FORMAT_R32_SINT,                            // format (int)
	//		static_cast<uint32_t>(offsetof(InstanceData, textureIndex))
	//	});

	//	return attributeDescriptions;
	//}

	///*
	//* Note: tinyobjloader doesn't expose any animation data. This is for rendering static mesh's
	//*/
	//void AvengModel::Builder::loadModel(const std::string& filepath)
	//{
	//	tinyobj::attrib_t attrib;				// This stores the position, color, normal and texture coord
	//	std::vector<tinyobj::shape_t> shapes;	// Index values for each face element
	//	std::vector<tinyobj::material_t> materials;
	//	std::string warn, err;

	//	if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filepath.c_str()))
	//	{
	//		std::cerr << "Warning: " << warn << "\nError: " << err << std::endl;
	//		throw std::runtime_error(warn + err);
	//	}

	//	vertices.clear();
	//	indices.clear();

	//	// Will track which vertices have been added to the Builder.vertices vector and store the position at which the vertex was originally added
	//	std::unordered_map<Vertex, uint32_t> uniqueVertices{};

	//	// For every face of our mesh
	//	for (const auto& shape : shapes) 
	//	{
	//		// For every vertex of the face
	//		for (const auto& index : shape.mesh.indices) 
	//		{
	//			Vertex vertex{};
	//			if (index.vertex_index >= 0) 
	//			{
	//				vertex.position = {
	//					attrib.vertices[3 * index.vertex_index + 0],
	//					attrib.vertices[3 * index.vertex_index + 1],	// The index calculations are a common convention for indexing into a vector as though it were a 2d matrix
	//					attrib.vertices[3 * index.vertex_index + 2],
	//				};

	//				vertex.color = {
	//					attrib.colors[3 * index.vertex_index + 0],
	//					attrib.colors[3 * index.vertex_index + 1],
	//					attrib.colors[3 * index.vertex_index + 2],
	//				};

	//			}

	//			if (index.normal_index >= 0) 
	//			{

	//				vertex.normal = {
	//					attrib.normals[3 * index.normal_index + 0],
	//					attrib.normals[3 * index.normal_index + 1],
	//					attrib.normals[3 * index.normal_index + 2],
	//				};
	//			}

	//			if (index.texcoord_index >= 0) 
	//			{
	//				
	//				vertex.texCoord = {
	//					attrib.texcoords[2 * index.texcoord_index + 0],
	//					1.0 - attrib.texcoords[2 * index.texcoord_index + 1],
	//				};
	//			}

	//			// If the vertex is new, we add it to the unique vertices map
	//			if (uniqueVertices.count(vertex) == 0) 
	//			{
	//				// The vertex's position in the Builder.vertices vector is given by the vertices vector's current size
	//				uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
	//				// Add it to the unique vertices map
	//				vertices.push_back(vertex);
	//			}

	//			// Add the position of the vertex to the Builder's indices vector
	//			indices.push_back(uniqueVertices[vertex]);

	//		}
	//	}

	//	//std::cout << filepath << " - Vertices: " << vertices.size() << std::endl;
	//	//std::cout << filepath << " - Indices: " << indices.size() << std::endl;
	//	
	//	// Debug UV coordinate ranges
	//	float minU = 1000.0f, maxU = -1000.0f, minV = 1000.0f, maxV = -1000.0f;
	//	for (const auto& vertex : vertices) {
	//		minU = std::min(minU, vertex.texCoord.x);
	//		maxU = std::max(maxU, vertex.texCoord.x);
	//		minV = std::min(minV, vertex.texCoord.y);
	//		maxV = std::max(maxV, vertex.texCoord.y);
	//	}
	//	std::cout << filepath << " - UV Range: U(" << minU << " to " << maxU << ") V(" << minV << " to " << maxV << ")" << std::endl;

	//}

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

		std::printf("%s: - model has a total of %i bone%s\n", __FUNCTION__, mBoneList.size(), mBoneList.size() == 1 ? "" : "s");
		std::printf("%s: - model has a total of %i animation%s\n", __FUNCTION__, numAnims, numAnims == 1 ? "" : "s");

		std::printf("%s: successfully loaded model '%s' (%s)\n", __FUNCTION__, filepath.c_str(), mModelFilename.c_str());
		return true;

	}

	bool AvengModel::createDescriptorSet(VkRenderData& renderData, std::vector<glm::mat4>& boneOffsetMatricesList, std::vector<int32_t>& boneParentIndexList) {

		/* init all SSBOs - These will take the current frame index into account, hence the vector usage */
		for (int i = 0; i < mBoneParentMatrixBuffers.size(); i++) {
			mBoneParentMatrixBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
				sizeof(boneParentIndexList.size()), 
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
				sizeof(boneOffsetMatricesList.size()), 
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
				mesh.processMesh(renderData, modelMesh, scene, assetDirectory, mTextures);

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

	std::vector<std::unique_ptr<AvengBuffer>> AvengModel::getBoneMatrixOffsetBuffers() {
		return mShaderBoneMatrixOffsetBuffers;
	}

	std::vector<std::unique_ptr<AvengBuffer>> AvengModel::getBoneParentBuffers() {
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
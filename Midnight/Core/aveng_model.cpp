#include <cassert>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include "aveng_model.h"
#include "Utils/aveng_utils.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader/tiny_obj_loader.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

namespace std {

	// This function allows us to take a vertex struct instance and hash it, for use by an unordered map key
	// This allows us to create vertex buffers which only contain unique vertices
	template<>
	struct hash<aveng::AvengModel::Vertex> {
		size_t operator()(aveng::AvengModel::Vertex const& vertex) const {
	
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

	//AvengModel::AvengModel(EngineDevice& device, const AvengModel::Builder& builder) 
	//	: engineDevice{ device }
	//{
	//	createVertexBuffers(builder.vertices);		
	//	createIndexBuffers(builder.indices);
	//}

	AvengModel::AvengModel(EngineDevice& device, std::vector<AvengModel::Vertex> vertices, std::vector<uint32_t> indices)
		: engineDevice{ device }
	{
		std::cout << "Instantiating Model..." << std::endl;
		createVertexBuffers(vertices); // The vertex shader takes input from a vertex buffer from `layout(location = n) in vec3 vertexAttribute`. The vertexAttribute is defined by the vertex Buffer
		createIndexBuffers(indices);
	}

	AvengModel::~AvengModel() 
	{
	}

	std::unique_ptr<AvengModel> AvengModel::createModelFromFile(EngineDevice& device, const std::string& filepath)
	{
		Builder builder{};
		builder.loadModel(filepath);
		return std::make_unique<AvengModel>(device, builder.vertices, builder.indices);
	}

	std::unique_ptr<AvengModel> AvengModel::drawTriangle(EngineDevice& device, glm::vec3 pos)
	{
		std::vector<AvengModel::Vertex> vertices { // vector
			{ { pos.x, pos.y, pos.z }, {1.0f, 1.0f, 1.0f }, {1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
			{ { pos.x + 0.5f,  pos.y + 0.5f, pos.z }, {1.0f, 1.0f, 1.0f }, {1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
			{ { pos.x - 0.5f,  pos.y + 0.5f, pos.z }, {1.0f, 1.0f, 1.0f }, {1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } }
		};

		std::vector<uint32_t> indices = { 0,1,2 };
		return std::make_unique<AvengModel>(device, vertices, indices);
	}

	/*
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
			// Host Coherent bit ensures the *data buffer is flushed to the device's buffer automatically, so we dont have to call the VkFlushMappedMemoryRanges
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		};

		// This takes care of vkMapMemory -> memcpy(vertices.data() ...) -> vkUnmapMemory
		stagingBuffer.map();
		stagingBuffer.writeToBuffer((void*)vertices.data());

		vertexBuffer = std::make_unique<AvengBuffer>(
			engineDevice,
			vertexSize,
			vertexCount,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			// Host Coherent bit ensures the *data buffer is flushed automatically so we dont have to call the VkFlushMappedMemoryRanges
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT // Memory properties - This is GPU heap allocated and super fast
		);

		// Copy memory from the staging buffer to the vertex buffer
		engineDevice.copyBuffer(stagingBuffer.getBuffer(), vertexBuffer->getBuffer(), bufferSize); // (src buffer, dst buffer, bufferSize)

		// Note:
		// The staging buffer is stack allocated, and does not need to be freed

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
			// Host Coherent bit ensures the *data buffer is flushed to the device's buffer automatically, so we dont have to call the VkFlushMappedMemoryRanges
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		};

		stagingBuffer.map();
		stagingBuffer.writeToBuffer((void*)indices.data());

		indexBuffer = std::make_unique<AvengBuffer>(
			engineDevice,
			indexSize,
			indexCount,
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			// Transfer bit ensures that this buffer's location in device memory will receive data from our transfer source bit
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT // // Memory properties - This is GPU heap allocated and super fast
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

	/*
	* @function AvengModel::Vertex::getBindingDescriptions
	* 1 of 2 requirements for describing how Vulkan
	* should pass data into the vertex shader
	*/
	std::vector<VkVertexInputBindingDescription> AvengModel::Vertex::getBindingDescriptions()
	{
		// This VkVertexInputBindingDescription corresponds to a single vertex buffer
		// it will occupy the binding at index 0.
		// The stride advances at sizeof(Vertex) bytes per vertex.
		// There is only 1 for now.
		/*
		    uint32_t             binding;
			uint32_t             stride;
			VkVertexInputRate    inputRate;
		*/
		std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
		bindingDescriptions[0].binding = 0;
		bindingDescriptions[0].stride = sizeof(Vertex);
		bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;		// Can be per vertex or per instance
		return bindingDescriptions;
	}

	/*
	* @function AvengModel::Vertex::getAttributeDescriptions
	* 2 of 2 required functions for describing how Vulkan
	* should pass data into the vertex shader
	*/
	std::vector<VkVertexInputAttributeDescription> AvengModel::Vertex::getAttributeDescriptions()
	{
		 /*
			uint32_t    location;	-- This specifies the location as assigned in the vertex shader i.e. layout( location = 0 ) 
			uint32_t    binding;	
			VkFormat    format;		-- Datatype: 2 components each 32bit signed floats
			uint32_t    offset;		-- type, membername. Calculates the byte offset of the position member from the Vertex struct
		 */
		// return { {0, 0, VK_FORMAT_R32G32_SFLOAT, 0} };
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};

		attributeDescriptions.push_back({ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position) });		// Vertex Positions
		attributeDescriptions.push_back({ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color) });			// Vertex colors
		attributeDescriptions.push_back({ 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) });		// Defines a surface's normal (the non-culled side)
		attributeDescriptions.push_back({ 3, 0, VK_FORMAT_R32G32_SFLOAT,	offsetof(Vertex, texCoord) });		// Texture coordinates

		return attributeDescriptions;

	}

	/*
	* Note: tinyobjloader doesn't expose any animation data. This is for rendering static mesh's
	*/
	void AvengModel::Builder::loadModel(const std::string& filepath)
	{
		tinyobj::attrib_t attrib;				// This stores the position, color, normal and texture coord
		std::vector<tinyobj::shape_t> shapes;	// Index values for each face element
		std::vector<tinyobj::material_t> materials;
		std::string warn, err;

		if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filepath.c_str()))
		{
			std::cerr << "Warning: " << warn << "\nError: " << err << std::endl;
			throw std::runtime_error(warn + err);
		}

		vertices.clear();
		indices.clear();

		// Will track which vertices have been added to the Builder.vertices vector and store the position at which the vertex wwas originally added
		std::unordered_map<Vertex, uint32_t> uniqueVertices{};

		// For every face of our mesh
		for (const auto& shape : shapes) 
		{
			// For every vertex of the face
			for (const auto& index : shape.mesh.indices) 
			{
				Vertex vertex{};
				if (index.vertex_index >= 0) 
				{
					vertex.position = {
						attrib.vertices[3 * index.vertex_index + 0],
						attrib.vertices[3 * index.vertex_index + 1],	// The index calculations are a common convention for indexing into a vector as though it were a 2d matrix
						attrib.vertices[3 * index.vertex_index + 2],
					};

					vertex.color = {
						attrib.colors[3 * index.vertex_index + 0],
						attrib.colors[3 * index.vertex_index + 1],
						attrib.colors[3 * index.vertex_index + 2],
					};

				}

				if (index.normal_index >= 0) 
				{

					vertex.normal = {
						attrib.normals[3 * index.normal_index + 0],
						attrib.normals[3 * index.normal_index + 1],
						attrib.normals[3 * index.normal_index + 2],
					};
				}

				if (index.texcoord_index >= 0) 
				{
					
					vertex.texCoord = {
						attrib.texcoords[2 * index.texcoord_index + 0],
						attrib.texcoords[2 * index.texcoord_index + 1],
					};
				}

				// If the vertex is new, we add it to the unique vertices map
				if (uniqueVertices.count(vertex) == 0) 
				{
					// The vertex's position in the Builder.vertices vector is given by the vertices vector's current size
					uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
					// Add it to the unique vertices map
					vertices.push_back(vertex);
				}

				// Add the position of the vertex to the Builder's indices vector
				indices.push_back(uniqueVertices[vertex]);

			}
		}

		//std::cout << filepath << " - Vertices: " << vertices.size() << std::endl;
		//std::cout << filepath << " - Indices: " << indices.size() << std::endl;

	}

}